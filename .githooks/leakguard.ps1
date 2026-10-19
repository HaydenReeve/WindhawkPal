#Requires -Version 7
# Leak guard for git hooks. Blocks commits and pushes that carry personal or
# machine-specific data. Findings print the rule name and location only; the
# matched text is never echoed.
#
# Usage (called by the shims in this folder):
#   leakguard.ps1 pre-commit
#   leakguard.ps1 pre-push <remote name> <remote url>   (ref lines on stdin)

param(
    [Parameter(Mandatory)][ValidateSet('pre-commit', 'pre-push')][string]$Mode,
    [Parameter(ValueFromRemainingArguments)][string[]]$HookArgs
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$OutputEncoding = [Text.Encoding]::UTF8

function Fail([string]$Message) {
    [Console]::Error.WriteLine("leakguard: $Message")
    exit 1
}

# --- Preconditions (fail closed) ---------------------------------------------

if (-not (Get-Command gitleaks -ErrorAction SilentlyContinue)) {
    Fail 'gitleaks is not on PATH. Install it, for example: winget install --id Gitleaks.Gitleaks --exact'
}

$denylistPath = git config --type=path leakguard.denylist 2>$null
if (-not $denylistPath -or -not (Test-Path -LiteralPath $denylistPath -PathType Leaf)) {
    Fail 'denylist is not configured. Set it with: git config --local leakguard.denylist <path>'
}

# --- Terms ---------------------------------------------------------------------

$denyTerms = @(Get-Content -LiteralPath $denylistPath -Encoding utf8 |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and -not $_.StartsWith('#') } |
    Select-Object -Unique)

$identityTerms = [Collections.Generic.List[string]]::new()
$userName = git config user.name 2>$null
foreach ($value in @(
        ($env:USERNAME ?? $env:USER),
        ($env:COMPUTERNAME ?? [Environment]::MachineName),
        $userName,
        (git config user.email 2>$null))) {
    if ($value -and $value.Trim().Length -ge 3) { $identityTerms.Add($value.Trim()) }
}
if ($userName) {
    foreach ($word in $userName -split '\s+') {
        if ($word.Length -ge 3) { $identityTerms.Add($word) }
    }
}
$identityTerms = @($identityTerms | Select-Object -Unique)

$emailPattern = '[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'
$patternRules = [ordered]@{
    'profile-path' = '(?i)\b[A-Z]:[\\/]+Users[\\/]+[^\\/\s]+|(?<![\w.])/(?:Users|home)/[^/\s]+'
    'drive-path'   = '\b[A-Za-z]:[\\/]'
}

function Test-ContainsAny([string]$Text, [string[]]$Terms) {
    foreach ($term in $Terms) {
        if ($Text.IndexOf($term, [StringComparison]::OrdinalIgnoreCase) -ge 0) { return $true }
    }
    return $false
}

$findings = [Collections.Generic.List[string]]::new()

function Test-Line([string]$Text, [string]$Location) {
    foreach ($rule in $patternRules.Keys) {
        if ($Text -match $patternRules[$rule]) { $findings.Add("$rule $Location") }
    }
    foreach ($m in [regex]::Matches($Text, $emailPattern)) {
        if ($m.Value -notmatch '(?i)noreply') { $findings.Add("email $Location"); break }
    }
    if (Test-ContainsAny $Text $identityTerms) { $findings.Add("identity $Location") }
    if (Test-ContainsAny $Text $denyTerms) { $findings.Add("denylist $Location") }
}

# Scans the added lines of a unified diff produced with -U0.
function Test-Diff([string[]]$DiffLines, [string]$Prefix) {
    $file = $null; $newLine = 0; $oldLeft = 0; $newLeft = 0
    foreach ($line in $DiffLines) {
        if ($oldLeft -gt 0 -or $newLeft -gt 0) {
            if ($line.StartsWith('+')) {
                Test-Line $line.Substring(1) "$Prefix${file}:$newLine"
                $newLine++; $newLeft--
            }
            elseif ($line.StartsWith('-')) { $oldLeft-- }
            continue
        }
        if ($line -match '^\+\+\+ (?:b/)?(.*)$') { $file = $Matches[1] }
        elseif ($line -match '^@@ -\d+(?:,(\d+))? \+(\d+)(?:,(\d+))? @@') {
            $oldLeft = $Matches[1] ? [int]$Matches[1] : 1
            $newLine = [int]$Matches[2]
            $newLeft = $Matches[3] ? [int]$Matches[3] : 1
        }
    }
}

$diffOpts = @('-c', 'core.quotePath=false')

# --- Modes -----------------------------------------------------------------------

if ($Mode -eq 'pre-commit') {
    $scope = 'staged changes'
    Test-Diff @(git @diffOpts diff --cached -U0 --no-color --no-ext-diff) ''
    gitleaks git --pre-commit --staged --redact --no-banner
    if ($LASTEXITCODE -ne 0) { $findings.Add('credential staged changes (see gitleaks output above)') }
}
else {
    # pre-push: git passes <remote name> <url>. When pushing straight to a URL
    # or path, the name is that URL, so no remote-tracking refs exist to
    # exclude and every commit reachable from the pushed ref is scanned.
    $remote = $HookArgs ? $HookArgs[0] : ''
    $isConfigured = $remote -and (@(git remote) -contains $remote)
    $exclude = $isConfigured ? @('--not', "--remotes=$remote") : @()

    $commitCount = 0
    foreach ($refLine in [Console]::In.ReadToEnd() -split "`r?`n") {
        $parts = $refLine.Trim() -split '\s+'
        if ($parts.Count -lt 4) { continue }
        $localSha = $parts[1]
        if ($localSha -match '^0+$') { continue }   # branch deletion

        foreach ($sha in @(git rev-list $localSha @exclude)) {
            $commitCount++
            $short = $sha.Substring(0, 7)
            Test-Diff @(git @diffOpts show --format= -U0 --no-color --no-ext-diff --diff-merges=first-parent $sha) "${short}:"
            $n = 1
            foreach ($msgLine in @(git log -1 --format=%B $sha)) {
                Test-Line $msgLine "${short}:message:$n"; $n++
            }
        }

        $logOpts = (@($localSha) + $exclude) -join ' '
        gitleaks git --redact --no-banner "--log-opts=$logOpts"
        if ($LASTEXITCODE -ne 0) { $findings.Add("credential $($parts[0]) (see gitleaks output above)") }
    }
    $scope = "$commitCount pushed commit(s)"
}

# --- Report ------------------------------------------------------------------------

foreach ($f in $findings) { [Console]::Error.WriteLine("leakguard: $f") }
if ($findings.Count -gt 0) {
    $rules = ($findings | ForEach-Object { ($_ -split ' ')[0] } | Select-Object -Unique) -join ', '
    Fail "$($findings.Count) finding(s) in $scope [$rules]; $Mode blocked"
}
Write-Host "leakguard: clean ($scope)"
exit 0
