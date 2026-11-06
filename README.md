# Replace Start with Command Palette

Tap the Win key and PowerToys Command Palette opens in place of Start.

Start's search runs on Windows Search, and Windows Search is slow. Most of the menu is pins and recommendations. Command Palette opens fast, ranks apps well, and with extensions such as Everything, file search becomes useful again.

Your Win+key shortcuts keep working. Typing is buffered straight into the palette's search box, before it has finished appearing. Tap again to close it.

You can still access the traditional Start Menu with `Ctrl+Esc` or through the taskbar icon directly.

## Requirements

- Windows 11
- Windhawk 2.0 or later
- PowerToys Command Palette

## Install

The mod installs from its source file, `mods\replace-start-with-command-palette.wh.cpp`, through the Windhawk UI or the Windhawk CLI.

### Windhawk UI

1. Create a new mod in Windhawk.
2. Replace the template with the contents of `replace-start-with-command-palette.wh.cpp`.
3. Compile the mod.

### Windhawk CLI

Run the command from an elevated shell in the repository root, with Windhawk's install folder on `PATH`.

```powershell
windhawk-cli mod install --file mods\replace-start-with-command-palette.wh.cpp
```

After installing, the mod works with no Explorer restart.

## Settings

Each setting appears on the mod's Settings tab in Windhawk.

| Key | What it does | Default |
| --- | --- | --- |
| `action` | What a Win tap does: `palette` opens Command Palette, and `command` runs a command. | `palette` |
| `pageHotkey` | A Command Palette command hotkey that opens a page, such as an extension's search. Empty opens the palette's home page. | empty |
| `paletteHotkey` | Command Palette's activation hotkey, as set in Command Palette. A tap sends it to close the palette while the palette is in front. | `alt+win+32` (Alt+Win+Space) |
| `command` | The program, file, folder, or URI a Win tap opens when `action` is `command`. | empty |
| `arguments` | Arguments passed to the command. | empty |
| `mouseInput` | How the mod sees mouse clicks: `hook` uses a low-level mouse hook, and `rawInput` uses Raw Input, which never delays the mouse. Only one mod in Windhawk's UI access process can use Raw Input. | `hook` |

A hotkey's stored form is `[ctrl+][alt+][shift+][win+]<vk>`, where `<vk>` is the key's virtual-key code in decimal, so `32` is Space.

From an elevated shell, the CLI sets any key as a `key=value` pair.

```powershell
windhawk-cli mod settings set local@replace-start-with-command-palette <key>=<value>
```

## How it works

The mod runs in Windhawk's UI access process, so it also works while an elevated window has focus.

1. **Detect the tap.** A low-level keyboard hook keeps its own key-down table. A new key pressed while Win is down, or a mouse click, makes a chord. Repeats of a key held from before the Win press don't count, so a held push-to-talk key keeps the tap. Ctrl, Alt, or Shift still held at the Win release makes a chord. Left and right Win count alike.
2. **Mask Start.** On a tap, or a click with Win held, the hook sends the unassigned key VK `0xE8` before Windows sees the Win release, so the Start Menu stays shut.
3. **Summon.** The mod calls `AllowSetForegroundWindow` on the Command Palette process, then signals Command Palette's show event. With a page hotkey set, it sends that hotkey instead. With `action` set to `command`, it runs the command on its own thread. A tap while the palette is in front sends the palette hotkey, which closes it.
4. **Buffer and replay.** Keys typed after the tap are held and replayed into the palette when its search box is ready. In the palette, an Enter and the keys after it wait briefly so the results update first. After a 600 ms deadline, every held key, Enter included, replays to whichever window has focus.

## Limits

- While Sticky Keys is on, the mod does nothing.
- Command Palette must be running for a tap to open it.
- The show event and the VK `0xE8` mask rely on undocumented behaviour of Command Palette and Windows.
- While Windhawk's UI access process is stopped, the Win key opens Start.

## Debugging

Each symptom below lists its checks in order, from the most common cause.

### Turn on logs

Logs show what the mod sees on each tap.

1. In Windhawk, open the mod's **Advanced** tab.
2. Turn on **Debug logging** → **Mod logs**.
3. Select **Show log output**.

From an elevated shell, the CLI turns on the same logs.

```powershell
windhawk-cli mod config set local@replace-start-with-command-palette loggingEnabled true
```

| Log line | Meaning |
| --- | --- |
| `Keyboard hook failed: <error>` | The keyboard hook failed to install, so the mod sees no Win taps. |
| `Mouse hook failed: <error>` | The mouse hook failed to install, so a Win+click counts as a tap. |
| `Raw Input registration failed: <error>` | Raw Input failed to register, so a Win+click counts as a tap. Set `mouseInput` to `hook`. |
| `Command Palette is not running` | A tap found no `Microsoft.CmdPal.UI.exe` process, so typed keys replay to the window with focus. |
| `OpenEvent failed: <error>` | The mod found Command Palette but could not open its show event, so typed keys replay to the window with focus. |
| `SendInput sent <n> of <total>: <error>` | Windows accepted only part of the mod's injected input, which covers the Start mask, hotkeys, and replayed keys. |
| `ShellExecute failed: <code>, <error>` | The `command` setting failed to open. |

### A tap does nothing

The Start Menu stays shut and the palette stays hidden.

- Check Command Palette is running. If it isn't, the log shows `Command Palette is not running`.
- Check the log for `OpenEvent failed`, which means the show event is unavailable.
- With a page hotkey set, check it matches a command hotkey set in Command Palette.
- With `action` set to `command`, check the `command` setting holds a program, file, folder, or URI, and check the log for `ShellExecute failed`.

### A second tap leaves the palette open

- Check `paletteHotkey` matches Command Palette's activation hotkey.

### Start opens instead

The Win key reaches Windows unmasked.

- Check the mod is enabled with `windhawk-cli mod list`; the mod's line reads `enabled`.
- Check Sticky Keys is off.
- Check the log for `Keyboard hook failed`.
- Check the log for `SendInput sent`, which shows the mask key was blocked.

### Typed keys go missing

The keys typed after a tap go to a window other than the palette's search box.

- Check the palette appears within 600 ms of the tap; after that, held keys replay to the window with focus.
- Check the log for `SendInput sent`, which shows replayed keys were blocked.

## Tests

The unit tests in `tests\` compile the mod source against fake Windhawk headers and drive its hooks with synthetic events, so they never touch the desktop. They cover tap judgement, the Start mask, and the type-ahead buffer.

Run the tests from the repository root.

```powershell
.\tests\run.ps1
```

The script uses Windhawk's bundled clang when it is installed, and otherwise `clang++` on `PATH`. The `pre-push` hook runs the tests after the leak guard, and GitHub Actions runs them on every push and pull request.

## Licence

The mod is licensed under GPL-3.0; see [LICENSE](LICENSE).
