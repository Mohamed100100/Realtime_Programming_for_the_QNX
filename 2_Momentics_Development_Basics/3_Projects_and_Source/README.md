

# Project and Source Code Management in QNX Momentics IDE

## Overview

This module covers how the QNX Momentics IDE organizes, displays, and helps you navigate projects and source code. You will learn how projects structure your code, how to import and export projects, how to use the editor effectively, and how to leverage IDE features for code comprehension and navigation. These skills are essential for managing the exercises in the Realtime Programming and Drivers courses.

---

## Project Structure and Organization

The IDE organizes all source code into **projects**. A project is a directory (folder) containing your source files, build configurations, and metadata. The primary window into your projects is the **Project Explorer view**, which displays the complete contents of your project in a hierarchical tree.

### What You See in Project Explorer

When you expand a project in the Project Explorer, you see several categories of items:

| Item | Description | Example |
|------|-------------|---------|
| **Source files** | Your C/C++ implementation files | `main.c`, `utils.c`, `driver.cpp` |
| **Header files** | Include files with declarations and macros | `utils.h`, `config.h` |
| **Object files** | Compiled intermediate files (after building) | `main.o`, `utils.o` |
| **Executable files** | Linked binaries ready to run | `my_application` |
| **Binaries pseudo-folder** | Virtual folder showing all build outputs | Automatically populated by IDE |
| **Includes folder** | System and library header files used by your project | `stdio.h`, `pthread.h`, `sys/neutrino.h` |

The **Binaries** folder is a pseudo-directory — it doesn't exist on disk as a separate folder. The IDE scans your build output locations and presents all compiled binaries in one convenient view. This makes it easy to locate the executable you want to run or debug without navigating deep directory structures.

The **Includes** folder is also virtual. It shows all system header files that your project references during compilation. This is useful for understanding dependencies and quickly opening standard library headers to check function signatures or macro definitions.

```
┌─────────────────────────────────────────────────────────────────────┐
│              PROJECT EXPLORER VIEW                                     │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  MyProject                                                   │   │
│  │  ├── src/                                                    │   │
│  │  │   ├── main.c          ← double-click to open in editor   │   │
│  │  │   ├── utils.c                                              │   │
│  │  │   └── driver.cpp                                           │   │
│  │  ├── include/                                                │   │
│  │  │   ├── utils.h                                               │   │
│  │  │   └── config.h                                              │   │
│  │  ├── Binaries           ← pseudo-folder (IDE-generated)      │   │
│  │  │   ├── MyProject/                                          │   │
│  │  │   │   ├── main.o                                           │   │
│  │  │   │   ├── utils.o                                           │   │
│  │  │   │   └── my_application  ← linked executable              │   │
│  │  │   └── ...                                                  │   │
│  │  └── Includes           ← pseudo-folder (system headers)     │   │
│  │       ├── stdio.h                                             │   │
│  │       ├── pthread.h                                           │   │
│  │       ├── sys/neutrino.h                                      │   │
│  │       └── math.h                                              │   │
│  │                                                               │   │
│  │  [+] AnotherProject                                          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  • Folders with "/" are real directories on disk                     │
│  • "Binaries" and "Includes" are virtual — IDE constructs          │
│  • Double-click any file to open in editor                           │
│  • Right-click for context menu (build, debug, properties)           │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Importing Projects

For the course exercises, you will import pre-existing projects rather than creating them from scratch. The IDE supports importing projects from archive files (ZIP) or existing directories.

### Import from Archive File

This is the standard method for importing course exercise projects:

1. Select **File → Import** from the menu
2. Expand the **General** category
3. Choose **Existing Projects into Workspace**
4. Click **Next**
5. Select **Select archive file** option
6. Browse to or enter the path to your ZIP file
7. The IDE scans the archive and lists all projects found
8. Select the projects you want to import (or select all)
9. Click **Finish**

The projects are extracted into your workspace and appear in the Project Explorer. If the archive contains pre-compiled binaries, you may see the Binaries folder already populated.

```
┌─────────────────────────────────────────────────────────────────────┐
│              IMPORTING PROJECTS FROM ARCHIVE                         │
│                                                                      │
│  STEP 1: Open Import Dialog                                           │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  File → Import                                                        │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Import Wizard                                               │   │
│  │                                                             │   │
│  │  Select an import source:                                    │   │
│  │                                                             │   │
│  │  ▼ General                                                   │   │
│  │    ├── Existing Projects into Workspace  ← SELECT THIS       │   │
│  │    ├── Archive File                                              │   │
│  │    └── Preferences                                               │   │
│  │                                                             │   │
│  │  [Next >]  [Cancel]                                          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 2: Select Archive File                                          │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Import Projects                                               │   │
│  │                                                             │   │
│  │  [•] Select root directory    [Browse...]                   │   │
│  │  [•] Select archive file       [Browse...]  ← SELECT THIS     │   │
│  │                                                             │   │
│  │  Archive path: /path/to/exercises.zip  [Browse...]          │   │
│  │                                                             │   │
│  │  Projects found in archive:                                  │   │
│  │  ☑ exercise_01_ipc                                           │   │
│  │  ☑ exercise_02_threads                                         │   │
│  │  ☑ exercise_03_resource_mgr                                  │   │
│  │  ☑ exercise_04_timers                                        │   │
│  │  ☑ exercise_05_interrupts                                     │   │
│  │                                                             │   │
│  │  [Select All]  [Deselect All]                                │   │
│  │                                                             │   │
│  │  [Finish]  [Cancel]                                          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  Click "Finish" — projects appear in Project Explorer                  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Exporting Projects

When you need to share your work or submit exercises, you can export projects to an archive file. It is good practice to clean the project first to remove build artifacts, making the archive smaller and containing only source code.

### Export Workflow

1. **Clean the project** (optional but recommended)
   - Select **Project → Clean...**
   - Choose your project(s)
   - This removes all compiled binaries and object files

2. **Export to archive**
   - Select **File → Export**
   - Expand **General** category
   - Choose **Archive File**
   - Click **Next**
   - Select the projects to export
   - Enter the destination archive file path
   - Click **Finish**

This produces a ZIP file containing only source files, headers, and project metadata — ideal for sharing, backup, or submission when not using source control.

---

## Working with Source Files

### Opening Files

Double-click any file in the Project Explorer to open it in the editor. This applies to:
- Source files (`.c`, `.cpp`)
- Header files (`.h`, `.hpp`)
- Build files and scripts
- Any text-based configuration file

The file opens in a tabbed editor window. Multiple files can be open simultaneously, with tabs allowing quick switching between them.

### Maximizing and Restoring Editor Windows

To focus on a single file without distractions:
- **Double-click the editor tab or title bar** to maximize the editor to full window
- **Double-click again** to restore to the previous size and layout

This is useful when reading complex code or comparing sections of a long file.

### Outline View

The **Outline view** provides a structural summary of the currently active source file. It updates automatically as you switch between editor tabs.

What the Outline view displays:
- **Functions** — All function definitions with their signatures
- **Variables** — Global and static variables
- **Type definitions** — `struct`, `union`, `enum`, `typedef` declarations
- **Macros** — `#define` constants and macro functions
- **Include directives** — List of `#include` statements

Click any item in the Outline view to jump directly to that location in the source file. This is invaluable for navigating large files without scrolling.

```
┌─────────────────────────────────────────────────────────────────────┐
│              EDITOR AND OUTLINE VIEW WORKFLOW                          │
│                                                                      │
│  ┌──────────────────────────────────────────┬──────────────────┐  │
│  │                                          │                  │  │
│  │  EDITOR: main.c                          │  OUTLINE VIEW    │  │
│  │  ──────────────────────────────────────  │  ───────────────  │  │
│  │                                          │                  │  │
│  │  #include <stdio.h>                      │  ▼ main.c        │  │
│  │  #include <pthread.h>                    │    ├── init()    │  │
│  │                                          │    ├── main()    │  │
│  │  #define EXAMPLE_NAME "demo"             │    ├── cleanup() │  │
│  │                                          │    ├── buffer    │  │
│  │  int buffer[1024];                       │    ├── count     │  │
│  │  int count = 0;                          │    ├── EXAMPLE_NAME│  │
│  │                                          │    ├── <stdio.h> │  │
│  │  void init(void) {                       │    └── <pthread.h>│  │
│  │      // initialization                   │                  │  │
│  │  }                                       │  Click "main()"  │  │
│  │                                          │  → jumps to      │  │
│  │  int main(int argc, char *argv[]) {      │    main() in     │  │
│  │      init();                             │    editor        │  │
│  │      // ...                              │                  │  │
│  │      return 0;                           │  Click "count"   │  │
│  │  }                                       │  → jumps to      │  │
│  │                                          │    declaration   │  │
│  │                                          │                  │  │
│  └──────────────────────────────────────────┴──────────────────┘  │
│                                                                      │
│  Double-click title bar → maximize editor to full window             │
│  Double-click again → restore to normal layout                      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Code Editing Features

### Code Completion (Ctrl+Space)

The IDE provides intelligent code completion that suggests functions, variables, structures, and code templates as you type. This is particularly useful for QNX-specific functions and POSIX APIs where you may not remember exact signatures.

How to use code completion:
1. Start typing a function name or identifier
2. Press **Ctrl+Space**
3. A popup appears with matching suggestions
4. Use arrow keys to navigate suggestions
5. Press **Enter** to insert the selected item
6. Tooltip hints show parameter types and descriptions

This works for standard library functions (`atan`, `printf`, `pthread_create`), QNX kernel calls (`MsgSend`, `ChannelCreate`), and your own project functions.

### Hover Help

Hovering the mouse cursor over a function name, variable, or macro displays a tooltip with summary information. This provides quick reference without leaving your current file or opening documentation.

Hover help works for:
- **Library functions** — Shows function signature and brief description
- **Custom functions** — Shows declaration and any Doxygen comments
- **Macros** — Shows the macro expansion
- **Variables** — Shows type and declaration location
- **QNX-specific APIs** — Shows kernel call documentation

This is invaluable when encountering unfamiliar APIs. For example, hovering over `dispatch_create_channel` reveals its purpose and parameters without needing to look up the documentation manually.

### Include File Management (Ctrl+Shift+N)

When you use a function from a library that requires a specific header file, the IDE can automatically add the necessary `#include` directive. This saves time and prevents missing header errors.

Two methods to add includes:
- **Keyboard shortcut**: Select the identifier, press **Ctrl+Shift+N**
- **Menu method**: Right-click the identifier → **Source → Add Include**

The IDE determines the correct header based on the identifier and inserts it at the appropriate location in your file (typically after existing `#include` lines). For example, using `atan` from the math library will add `#include <math.h>`.

```
┌─────────────────────────────────────────────────────────────────────┐
│              INCLUDE FILE AUTO-ADDITION                                │
│                                                                      │
│  BEFORE:                                                              │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  #include <stdio.h>                                           │   │
│  │  #include <pthread.h>                                         │   │
│  │                                                               │   │
│  │  int main() {                                                 │   │
│  │      double result = atan(1.0);  ← math.h not included        │   │
│  │      // ...                                                   │   │
│  │  }                                                            │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ACTION: Double-click "atan", press Ctrl+Shift+N                     │
│                                                                      │
│  AFTER:                                                               │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  #include <stdio.h>                                           │   │
│  │  #include <pthread.h>                                         │   │
│  │  #include <math.h>          ← ADDED AUTOMATICALLY              │   │
│  │                                                               │   │
│  │  int main() {                                                 │   │
│  │      double result = atan(1.0);  ← now compiles correctly      │   │
│  │      // ...                                                   │   │
│  │  }                                                            │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  Alternative: Right-click "atan" → Source → Add Include              │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Go to Definition (F3)

The **F3** key is a powerful navigation shortcut that jumps from a function call to its definition, or from a variable use to its declaration. This works across your entire project, including multiple files.

Usage:
1. Place cursor on (or select) a function name or variable
2. Press **F3**
3. Editor opens the defining file and scrolls to the exact location

This is equivalent to using the Outline view but works from any context — even inside deeply nested code where you don't want to lose your place.

### Find and Replace (Ctrl+F)

The standard find dialog searches within the current file. Press **Ctrl+F**, enter your search term, and navigate through matches with **Find Next** and **Find Previous**. The replace option allows bulk modifications.

Features:
- Case-sensitive and whole-word matching options
- Regular expression support for advanced patterns
- Search direction (forward/backward)
- Replace individual matches or all at once

### Multi-File Search (Ctrl+H)

For searching across your entire project or workspace, use **Ctrl+H** to open the Search dialog. This finds occurrences of identifiers, strings, or patterns across multiple files.

Search scope options:
- **Workspace** — All projects in your current workspace
- **Enclosing project** — Only the project containing the current file
- **Working set** — A custom-defined subset of projects

Results appear in a dedicated Search view, listing every file and line number where the match occurs. Double-click any result to open that file at the exact location. This is essential for refactoring — finding all uses of a function before renaming it, or locating all references to a constant before modifying its value.

```
┌─────────────────────────────────────────────────────────────────────┐
│              MULTI-FILE SEARCH (Ctrl+H)                                │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Search Dialog                                               │   │
│  │                                                             │   │
│  │  [•] File Search                                             │   │
│  │                                                             │   │
│  │  Search string: [EOK________________________]            │   │
│  │                                                             │   │
│  │  [•] Case sensitive    [ ] Regular expression              │   │
│  │                                                             │   │
│  │  Scope:                                                     │   │
│  │  (•) Workspace                                              │   │
│  │  ( ) Selected resources                                     │   │
│  │  ( ) Enclosing project     ← current project only            │   │
│  │  ( ) Working set                                            │   │
│  │                                                             │   │
│  │              [Cancel]  [Search]                             │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  RESULTS:                                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Search Results                                               │   │
│  │                                                             │   │
│  │  Found 12 matches for 'EOK' in workspace:                   │   │
│  │                                                             │   │
│  │  main.c (line 45):   if (status == EOK) {                   │   │
│  │  main.c (line 78):   return EOK;                            │   │
│  │  utils.c (line 12):  #define SUCCESS EOK                    │   │
│  │  driver.c (line 89): rc = MsgSend(coid, &msg, sizeof(msg), │   │
│  │                      if (rc == EOK) {                       │   │
│  │  ...                                                        │   │
│  │                                                             │   │
│  │  Double-click any result → jump to that file and line      │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  Use for: refactoring, understanding code usage, finding bugs        │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Standard Editing Shortcuts

| Shortcut | Action | Context |
|----------|--------|---------|
| **Ctrl+Space** | Code completion | Anywhere in editor |
| **Ctrl+C** | Copy selection | Standard text editing |
| **Ctrl+X** | Cut selection | Standard text editing |
| **Ctrl+V** | Paste | Standard text editing |
| **Ctrl+Z** | Undo | Revert last change |
| **Ctrl+Y** | Redo | Reapply undone change |
| **F3** | Go to definition | On function/variable name |
| **Ctrl+Shift+N** | Add include for identifier | On library function name |
| **Ctrl+F** | Find in current file | Anywhere |
| **Ctrl+H** | Search across files | Anywhere |
| **Ctrl+M** | Maximize/restore current view | Any view or editor |

---

## Summary

| Task | Method | Shortcut/Menu |
|------|--------|---------------|
| Open file | Double-click in Project Explorer | — |
| Maximize editor | Double-click title bar | Ctrl+M |
| See file structure | Outline view | Auto-updates with active editor |
| Import projects | File → Import → General → Existing Projects | — |
| Export projects | File → Export → General → Archive File | — |
| Code completion | Ctrl+Space | — |
| Get function help | Hover mouse over identifier | — |
| Add missing include | Ctrl+Shift+N or Source → Add Include | Ctrl+Shift+N |
| Jump to definition | F3 | F3 |
| Find in file | Ctrl+F | Ctrl+F |
| Search across project | Ctrl+H | Ctrl+H |

The Project Explorer, editor, and Outline view form the core of your daily workflow. Mastering navigation shortcuts like F3, code completion with Ctrl+Space, and multi-file search with Ctrl+H significantly improves productivity when working with QNX codebases.

---


*Happy coding!* 🚀
