<img width="1108" height="621" alt="Capture" src="https://github.com/user-attachments/assets/1da93dee-abfd-4e14-98ca-dd9e3c7991bf" />


# RunicShell26

Copyright (C) 2026, Roberto J Dohnert

## Disclosure

Microsoft Windows is a registered trademark of Microsoft Corporation.
KornShell is a registered trademark of AT&T Research released under the
Eclipse Public License. Originally developed by David Korn.

This project is neither endorsed nor affiliated with Microsoft Corporation or any 
employee of Microsoft nor is it endorsed or affiliated with AT&T Research or any employee 
of AT&T.

## Overview

RunicShell26 is a custom Windows command shell written in C++. It is designed as a ksh-style shell for local command execution, scripting, history, lightweight expression handling, and background job control.

## ksh93 Compliance

RunicShell26 is intended to be ksh93 compliant.

This project was rewritten for:

- Windows Server 2025
- Windows Server 2022
- Windows 11 Home/Pro/Enterprise
- Windows 10 Home/Pro/Enterprise

While originally written for Windows Server 2003 64-bit, it is unknown whether the rewrite will run on Windows Server 2003.

## Version

- (2.0.1-2026) (2nd generation)

## History

In 2005, I wrote this shell because I needed a native ksh-type shell for automation tasks on the new Windows Server 2003 64-bit release from Microsoft. Monad (Powershell) at the time was not very stable and was complicated for the staff.

I left that job in 2008 and left the shell behind. Fast forward to 2024: the company went bankrupt, and I was contacted by an old coworker who still worked there while they were selling old equipment. I bought some of the Dell servers they had for CNC function that still ran Windows Server 2003.

I was originally going to repurpose those systems for a Linux distribution that I created. Upon exploring the systems, I found they were still running the shell program I wrote all those years ago.

I reached back out and asked if I could get the old source code and if I could have ownership. It was granted, and I set out to rework and recompile the source to run on Windows Server 2022 Core and Desktop.

I reworked the code and improved/added additional functionality including:

1. Core shell with prompt.
2. Improved built-ins for shell control and scripting tools (`exit`, `source`, `typeset`, `[[`, `cd`, `print`, `echo`).
3. Added command history, history recall, and line editing (arrow keys, home/end, delete). Added history persistence
   and history file is saved in the users %APPDATA% folder in their home directory as rsh_history.txt
4. Added tab completion for built-ins and executable names from system locations, Program Files, and `$PATH`.
4. Added arithmetic parser/evaluator and `math` built-in.
6. Improved background job management (`jobs`, `fg`, `kill`, `wait`, trailing `&` support).
7. Improved script execution with positional parameters and script decoding.
8. Script compatibility with bash, ksh, and PowerShell scripts.

Yes, it is named after me, with my initials as the first three letters.

## What RunicShell26 Can Do

- Run interactively with command input, editing, and history.
- Execute external programs directly with `cmd.exe` fallback.
- Run script files and pass positional script arguments.
- Expand shell-style variables and arithmetic expressions.
- Track and manage background jobs.
- Perform command completion using built-ins and executables available on `$PATH`.
- Evaluate simple ksh-style conditional expressions.

## Built-In Functions and Commands

Available built-ins include:

- `exit`
- `source`
- `.` (dot, alias of `source`)
- `typeset`
- `[[`
- `cd`
- `print`
- `echo`
- `history`
- `complete`
- `math`
- `jobs`
- `fg`
- `kill`
- `wait`

## Built-in notes:

- `history` supports recall forms such as `!!`, `!N`, `!-N`, and `!prefix`.
- `complete [prefix]` prints matching completion candidates.
- `math <expression>` evaluates arithmetic expressions.
- Conditional support currently includes regex test (`=~`) and basic string equality (`==`).
- `var=value` assignments update shell/environment variables.
- `source`/`.` executes script files in the current shell context.

## Installation

1. Download the `.msi` file from the Releases section.
2. Run the `.msi` file and follow the installer instructions
3. accept the license agreement and continue.

## Notes:

- Installation requires Administrator privileges.
- On Windows Server Core, use the CLI install script (also requires Administrator privileges).
- If you do not want to install and are afraid of commitment, download the source package,
  which includes a precompiled binary you can run from any directory.

## Uninstall

(Thats a joke right?  I mean c'mon people why would you want to uninstall 
all this awesomeness?  I will be forever tainting your system BWAHAHAHAHAHAHA,
If you want to uninstall you have to do a clean system reinstall. BWAHAHAHAHAHA
Envision me in tears. Sad, Very Sad, I dont like goodbyes)

In all seriousness: An uninstall script is included in the installer folder in the source.
or go "Settings --> Installed Apps" find RunicShell26 and uninstall.

## How To Launch

If not installed, open Command Prompt or PowerShell in the project folder and run:

```powershell
rsh.exe
```

If installed into `Windows\System32`, run:

```powershell
rsh.exe
```

### Script Mode

1. Run:

```powershell
rsh.exe script.rsh arg1 arg2
```

2. Script positional references:

- `$0` script name
- `$1..$N` script arguments
- `$#` argument count
- `$@` joined argument list

## How To Use

### Command Line Editing

- Up/Down: browse history.
- Left/Right: move cursor.
- Home/End: jump to start/end.
- Backspace/Delete: remove characters.
- Tab: complete commands and executable names.

### Background Jobs

- Add `&` at the end of a command to run it in the background.
- `jobs`: list jobs.
- `fg <job id>`: bring a job to foreground and wait.
- `kill <job id>`: terminate a background job.
- `wait [job id]`: wait for one or all background jobs.

### Examples

```powershell
echo Hello World
cd C:\Windows
math 2 + 5 * 3
notepad.exe &
jobs
fg 1
history
!!
```

## Build Instructions (Windows Server 2025 / 2022)

### Requirements

- Microsoft Visual C++ Build Tools (`cl.exe`)
- Windows SDK

### Build Command

From the project directory:

```powershell
cl.exe /Zi /EHsc /nologo /Fersh.exe rsh.cpp
```

This matches the VS Code task style:

```powershell
cl.exe /Zi /EHsc /nologo /Fe${fileDirname}\${fileBasenameNoExtension}.exe ${file}
```

After build:

- Launch shell with `rsh.exe`
- Or run a script with `rsh.exe <script-file> [args...]`

## Making a profile in Windows Terminal

To make a profile in Windows Terminal do the following.

1. Launch Windows Terminal
2. Click on the down arrow beside the current tab that shows your profiles
3. Click on Settings
4. Scroll down and click on "Add A New Profile"
5. Go to "Duplicate a Profile" and Choose 'Command Prompt' and click "Duplicate"
6. On "Name" change that to RunicShell26 or whatever you would like
7. On command line change (%SystemRoot%\System32\cmd.exe) to (%SystemRoot%\System32\rsh.exe)
8. Click on "Save"
9. Restart Windows Terminal
10. Click the down arrow beside the tab to choose profiles and choose the RunicShell26 profile you created
11. Go grab a cold drink and a snack and enjoy playing with your new shell

## Compatibility

RunicShell26 is intended for Windows server environments, specifically:

- Windows Server 2025
- Windows Server 2022

It can also run on workstation and desktop environments:

- Windows 10 Home/Pro/Enterprise
- Windows 11 Home/Pro/Enterprise

## Script compatibility:

- ksh93 compliant and can run ksh scripts
- bash 5.3 (scripts)
- PowerShell 5 and 7 (scripts)

## Notes

- If you downloaded the program and liked it GREAT!!! If you found it useful AWESOME!!! Whichever or thank you for looking
- Not intended to be a full POSIX-compliant shell.
- `$` is currently used as an indicator of what shell you are in as well as a placeholder, and returns `[cmd_sub_output]`.
- Some ksh-style features are intentionally simplified in this shell.
- Command execution status is currently treated as success in the normal execution path.
- Not compatible with Oh-My-Posh so its not pretty nor sexy
- In 2005 I was looking for simple amd thats what I set out to make. Simple, lightwright and able to run ksh scripts. If you 
  write me to tell me my skill as a programmer sucks there is a 99.9% chance I will ignore you, if I dont ignore you
  I will laugh at you.
