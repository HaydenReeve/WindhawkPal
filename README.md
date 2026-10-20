# Replace Start with Command Palette

On Windows when you press the `Win` key normally the Start Menu opens. With this mod, PowerToys Command Palette opens instead.

The Start Menu's search runs on Windows Search: It's slow. _Really_ slow. Most of the menu is generally space you never use. In my workflows, I find I basically never want the Start Menu, it's just a useful quick launcher via `Win + "Program Name"`. Command Palette is faster, ranks apps well, and extensions such as Everything make file search excellent.

This mod handles masking that `Win` shortcut without interfering with your other usual hotkeys. It also smartly attaches focus, buffers any search input, and generally just makes life a little nicer.

The real start menu is still accessible through the normal channels, such as `Ctrl+Esc` or clicking the Start button with the mouse.

## Requirements

- Windows 11
- Windhawk 2.0
- PowerToys Command Palette

## Install

The mod installs from its source file, `mods\replace-start-with-command-palette.wh.cpp`, through the Windhawk UI or the Windhawk CLI.

### Windhawk UI

Replace Start with Command Palette is coming soon to the Windhawk mod gallery.

For now, you can install it manually via the UI like so:

1. Create a new mod in Windhawk.
2. Replace the template with the contents of `replace-start-with-command-palette.wh.cpp`.
3. Compile the mod.

These steps follow Windhawk's editor; check Windhawk's own documentation if something has drifted.

### Windhawk CLI

From an elevated shell in the repository root, run `windhawk-cli.exe` from Windhawk's install folder:

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

A hotkey's stored form is `[ctrl+][alt+][shift+][win+]<vk>`, where `<vk>` is the key's virtual-key code in decimal, so `32` is Space.

The CLI sets any key as a `key=value` pair:

```powershell
windhawk-cli mod settings set local@replace-start-with-command-palette <key>=<value>
```

## How it works

The mod runs in Windhawk's UI access process, so it also works while an elevated window has focus.

1. **Detect the tap.** A low-level keyboard hook keeps its own key-down table. A new key pressed while Win is down, or a mouse click, makes a chord. Repeats of a key held from before the Win press don't count, so a held push-to-talk key keeps the tap. Left and right Win count alike.
2. **Mask Start.** On a tap, the hook sends the unassigned key VK `0xE8` before Windows sees the Win release, so the Start menu stays shut.
3. **Summon.** The mod calls `AllowSetForegroundWindow` on the Command Palette process, then signals Command Palette's show event. With a page hotkey set, it sends that hotkey instead. With `action` set to `command`, it runs the command on its own thread. A tap while the palette is in front sends the palette hotkey, which closes it.
4. **Buffer and replay.** Keys typed after the tap are held and replayed into the palette on its first focus event, or 50 ms after it takes the foreground. In the palette, an Enter and the keys after it wait a further 150 ms, so the results update before Enter lands. After a 400 ms deadline, every held key, Enter included, replays to whichever window has focus.

## Limits

The code sets these bounds on what the mod does.

- While Sticky Keys is on, the mod does nothing.
- A modifier held from before the tap applies live, not to the replayed keys.
- Command Palette must be running for a tap to open it.
- The show event and the VK `0xE8` mask rely on undocumented behaviour of Command Palette and Windows.
- While the mod's process is stopped, the Win key opens Start.

## Debugging

Each symptom below lists its checks in order, from the most common cause.

### Turn on logs

Logs show what the mod sees on each tap.

1. In Windhawk, open the mod's **Advanced** tab.
2. Turn on **Debug logging** → **Mod logs**.
3. Select **Show log output**.

The CLI turns on the same logs:

```powershell
windhawk-cli mod config set local@replace-start-with-command-palette loggingEnabled true
```

| Log line | Meaning |
| --- | --- |
| `Keyboard hook failed: <error>` | The keyboard hook failed to install, so the mod sees no Win taps. |
| `Mouse hook failed: <error>` | The mouse hook failed to install, so a Win+click counts as a tap. |
| `Command Palette is not running` | A tap found no `Microsoft.CmdPal.UI.exe` process, so typed keys replay to the window with focus. |
| `OpenEvent failed: <error>` | The mod found Command Palette but could not open its show event, so typed keys replay to the window with focus. |
| `SendInput sent <n> of <total>: <error>` | Windows accepted only part of the mod's injected input, which covers the Start mask, hotkeys, and replayed keys. |
| `ShellExecute failed: <code>, <error>` | The `command` setting failed to open. |

### A tap does nothing

The Start menu stays shut and the palette stays hidden.

- Check Command Palette is running; the log shows `Command Palette is not running` when it isn't.
- Check the log for `OpenEvent failed`, which means the show event is unavailable.
- With a page hotkey set, check it matches a command hotkey set in Command Palette.
- With `action` set to `command`, check the `command` setting holds a program, file, folder, or URI, and check the log for `ShellExecute failed`.
- If a second tap fails to close the palette, check `paletteHotkey` matches Command Palette's activation hotkey.

### Start opens instead

The Win key reaches Windows unmasked.

- Check the mod is enabled with `windhawk-cli mod list`; the mod's line reads `enabled`.
- Check Sticky Keys is off.
- Check no other key went down and no mouse button was clicked while Win was held, since either makes a chord.
- Check the log for `Keyboard hook failed`.
- Check the log for `SendInput sent`, which shows the mask key was blocked.

### Typed keys go missing

The keys typed after a tap land outside the search box.

- Check the palette appears within 400 ms of the tap; after that, held keys replay to the window with focus.
- Check the log for `SendInput sent`, which shows replayed keys were blocked.
- Check for a modifier held from before the tap, which applies live to the window with focus.

## Licence

The mod is licensed under GPL-3.0; see [LICENSE](LICENSE).
