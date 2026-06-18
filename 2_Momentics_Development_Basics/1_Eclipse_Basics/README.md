

# Eclipse IDE Basics for QNX Momentics

## Overview

The QNX Momentics IDE is built on the **Eclipse** open-source platform — a Java-based framework designed for creating integrated development environments. This means Momentics inherits Eclipse's core concepts, terminology, and behaviors. Understanding these fundamentals — editors, views, and perspectives — is essential for efficient navigation and use of the IDE during QNX development.

---

## Core Eclipse Concepts

Eclipse organizes the development environment around three fundamental building blocks:

| Concept | Definition | Purpose |
|---------|-----------|---------|
| **Editor** | Window for editing or browsing resources | Write and modify source code, configuration files |
| **View** | Panel displaying information or navigation aids | Explore projects, inspect variables, browse targets |
| **Perspective** | Grouping of views and editors for a specific task | Organize workspace layout for editing, debugging, or analysis |

These three elements work together to create a flexible, task-oriented development environment. You switch between perspectives as your work changes from writing code to debugging to system analysis.

---

## Editors

Editors are the primary interface for modifying resources in your project. They open in the center of the IDE window and provide syntax highlighting, code completion, and navigation features.

**Key characteristics of editors:**

- Open by **double-clicking** a file in the Project Explorer or other navigation views
- Multiple editors can be open simultaneously, arranged in tabs
- Editors are typically shared across perspectives — the same source file editor appears in both C++ and Debug perspectives
- Context-aware features like code completion and error markers appear as you type

**Common editor types in Momentics:**

| Editor | File Types | Features |
|--------|-----------|----------|
| C/C++ Editor | `.c`, `.cpp`, `.h` | Syntax highlighting, code folding, outline integration |
| Buildfile Editor | `.build` | QNX image filesystem build script editing |
| XML Editor | `.xml`, `.launch` | Structured editing, validation |
| Text Editor | `.txt`, `.md` | Plain text with basic formatting |

---

## Views

Views are panels that provide information, navigation, and tools without directly editing content. They surround the editor area and can be rearranged, resized, minimized, or maximized to suit your workflow.

**What views do:**

- **Navigate** your project structure, files, and symbols
- **Inspect** running processes, threads, and memory on target systems
- **Display** search results, build output, console messages, and error lists
- **Control** debugging sessions, breakpoints, and variable watches

**Common views in Momentics:**

| View | Purpose | Typical Perspective |
|------|---------|-------------------|
| **Project Explorer** | Browse project files, folders, and resources | C/C++ |
| **Outline** | Show functions, variables, and includes in current file | C/C++ |
| **Build Targets** | Manage build configurations and variants | C/C++ |
| **Console** | Display build output, program output, and system messages | C/C++, Debug |
| **Variables** | Inspect local and global variables during debugging | Debug |
| **Breakpoints** | Manage breakpoints (enable, disable, remove) | Debug |
| **Debug** | Control execution (step, resume, terminate) and show call stack | Debug |
| **Target Navigator** | Browse processes and threads on connected QNX targets | QNX System |
| **Memory** | Examine raw memory contents at specific addresses | Debug |
| **Search** | Show results for text searches across projects | Any |

**Manipulating views:**

You can customize the layout by:
- **Dragging** view borders to resize panels
- **Clicking** the minimize button to collapse a view to the sidebar
- **Clicking** the maximize button to expand a view to full window
- **Dragging** view tabs to reposition them within or between perspectives

---

## Perspectives

A perspective is a **predefined arrangement of views and editors** optimized for a specific development task. Rather than manually opening and closing individual views, you switch perspectives to instantly reconfigure the entire workspace layout.

**Available perspectives in Momentics:**

| Perspective | Purpose | Key Views Included |
|-------------|---------|-------------------|
| **C/C++** | Code editing and project building | Project Explorer, Outline, Build Targets, Console |
| **Debug** | Debugging running programs | Variables, Breakpoints, Debug (call stack), Console, Memory |
| **QNX System** | Target system inspection and profiling | Target Navigator, System Information, Memory |
| **QNX Analysis** | Performance analysis and profiling | Analysis sessions, profiling results, trace data |
| **Team** | Version control operations (Git, SVN) | Repositories, History, Synchronize |

**Switching perspectives:**

There are multiple ways to change perspectives:

1. **Perspective toolbar icons** — Quick access icons in the top-right corner of the IDE window. Click an icon to switch instantly.

2. **Window menu** — Select `Window → Perspective → Open Perspective` to see commonly used perspectives. Select `Other...` to access the full list.

3. **Open Perspective dialog** — A dedicated dialog showing all available perspectives, optionally filtered by category.

```
┌─────────────────────────────────────────────────────────────────────┐
│              PERSPECTIVE SWITCHING METHODS                           │
│                                                                      │
│  METHOD 1: Toolbar Icons (Fastest)                                   │
│  ─────────────────────────────────                                   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  [C/C++] [Debug] [QNX System] [Analysis] [+]                │   │
│  │                                                             │   │
│  │  Click any icon to switch to that perspective immediately.    │   │
│  │  Click [+] to open additional perspectives.                   │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  METHOD 2: Window Menu                                               │
│  ──────────────────────                                              │
│                                                                      │
│  Window → Perspective → Open Perspective → [C/C++]                   │
│  Window → Perspective → Open Perspective → Other... → [Full List]    │
│                                                                      │
│  METHOD 3: Open Perspective Dialog                                   │
│  ─────────────────────────────────                                   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Open Perspective Dialog                                     │   │
│  │                                                             │   │
│  │  Type filter text: [__________]                            │   │
│  │                                                             │   │
│  │  ┌─ C/C++ ─────────────────────────────┐                    │   │
│  │  │  • C/C++                             │                    │   │
│  │  │  • Debug                             │                    │   │
│  │  ├─ QNX ──────────────────────────────┤                    │   │
│  │  │  • QNX System Information           │                    │   │
│  │  │  • QNX Analysis                     │                    │   │
│  │  ├─ Debug ────────────────────────────┤                    │   │
│  │  │  • Debug                             │                    │   │
│  │  └─ Other ────────────────────────────┘                    │   │
│  │                                                             │   │
│  │  [ ] Show all                                               │   │
│  │                                                             │   │
│  │              [Cancel]  [Open]                               │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  Filter by typing in the search box, or browse categories.           │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## The C/C++ Perspective in Detail

The C/C++ perspective is the primary workspace for writing and building code. It provides a comprehensive view of your project structure and source code.

**Layout overview:**

```
┌─────────────────────────────────────────────────────────────────────┐
│              C/C++ PERSPECTIVE LAYOUT                                  │
│                                                                      │
│  ┌─────────────────┬─────────────────────────┬──────────────────┐   │
│  │                 │                         │                  │   │
│  │  PROJECT        │    EDITOR AREA          │   OUTLINE        │   │
│  │  EXPLORER       │    (source code)        │   VIEW           │   │
│  │                 │                         │                  │   │
│  │  • Project 1    │    ┌─────────────────┐   │   • Functions      │   │
│  │    • src/       │    │  main.c         │   │   • main()       │   │
│  │      • main.c   │    │  ─────────────  │   │   • helper()     │   │
│  │      • utils.c  │    │  #include ...   │   │   • init()       │   │
│  │    • include/   │    │                 │   │   • Variables    │   │
│  │      • utils.h  │    │  int main() {   │   │   • count        │   │
│  │    • build/     │    │    ...          │   │   • buffer       │   │
│  │  • Project 2    │    │  }              │   │   • Includes     │   │
│  │                 │    └─────────────────┘   │   • <stdio.h>      │   │
│  │                 │                         │   • <pthread.h>    │   │
│  └─────────────────┴─────────────────────────┴──────────────────┘   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  CONSOLE / BUILD TARGETS / PROBLEMS (bottom panel)          │   │
│  │                                                             │   │
│  │  Build output:                                              │   │
│  │  make -k all                                                │   │
│  │  Building file: main.c                                      │   │
│  │  Building file: utils.c                                     │   │
│  │  Linking: my_application                                    │   │
│  │  Build complete. 0 errors, 0 warnings.                        │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  • Project Explorer: Navigate files, folders, and project resources  │
│  • Editor: Edit source code with syntax highlighting                 │
│  • Outline: Quick navigation to functions and symbols in current file │
│  • Console: Build messages, compiler output, and error reports         │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

**Project Explorer functionality:**

- Expand/collapse project folders to browse source files, headers, and build artifacts
- Right-click files for context menu options (build, debug, properties)
- Double-click files to open in the editor
- Icons indicate file type and build status

**Outline view functionality:**

- Automatically updates to show the structure of the currently active editor file
- Lists functions, variables, type definitions, and include directives
- Click any entry to jump directly to that location in the source code
- Useful for navigating large files without scrolling

---

## Adding and Finding Views

When you need a view that isn't visible in the current perspective, you can add it through the Window menu.

**Method 1: Quick access from Show View menu**

Select `Window → Show View` to see a list of commonly used views. Click any view name to open it in the current perspective.

**Method 2: Full view catalog**

Select `Window → Show View → Other...` to open the Show View dialog. This provides:

- **Categorized browsing** — Views grouped by functional area (QNX, Debug, General, Git, etc.)
- **Text filtering** — Type in the search box to filter views by name
- **Favorites** — Frequently used views appear at the top for quick access

```
┌─────────────────────────────────────────────────────────────────────┐
│              SHOW VIEW DIALOG                                          │
│                                                                      │
│  Window → Show View → Other...                                         │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Show View                                                   │   │
│  │                                                             │   │
│  │  Type filter text: [git________]  ← type to filter           │   │
│  │                                                             │   │
│  │  ┌─ General ───────────────────────────┐                  │   │
│  │  │  • Console                              │                  │   │
│  │  │  • Navigator                            │                  │   │
│  │  ├─ QNX ────────────────────────────────┤                  │   │
│  │  │  • Target Navigator                     │                  │   │
│  │  │  • System Builder                       │                  │   │
│  │  ├─ Debug ──────────────────────────────┤                  │   │
│  │  │  • Breakpoints                          │                  │   │
│  │  │  • Variables                            │                  │   │
│  │  ├─ Git ────────────────────────────────┤                  │   │
│  │  │  • Git Repositories                     │                  │   │
│  │  │  • Git Staging                            │                  │   │
│  │  │  • History                                │                  │   │
│  │  └─ Remote Systems ─────────────────────┘                  │   │
│  │     • TFTP Server                                         │   │
│  │                                                             │   │
│  │  Type "git" filters to show only Git-related views.         │   │
│  │  Type "system" filters to QNX System Builder views.           │   │
│  │                                                             │   │
│  │              [Cancel]  [Open]                               │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  Example: Type "TFTP" to find the TFTP Server view for configuring   │
│  network boot components in the QNX System Builder.                   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Practical Tips for Working with the IDE

### Customizing Perspectives

You can modify any perspective to match your personal workflow:

- **Add views** — Use `Window → Show View` to add panels to the current perspective
- **Rearrange views** — Drag view tabs to new positions within the window or to different monitor edges
- **Save custom layout** — `Window → Perspective → Save As...` to create a named custom perspective
- **Reset perspective** — `Window → Perspective → Reset` to restore the default layout

### Maximizing Editor Space

When focused on writing code, maximize the editor area:
- Double-click the editor tab to expand to full window
- Click the maximize button on the editor's title bar
- Use `Ctrl+M` (or `Cmd+M` on macOS) to toggle maximize for the current view/editor

### Working with Multiple Monitors

For complex debugging sessions, drag views to a secondary monitor:
- Drag the Debug view to one monitor showing call stack and thread state
- Keep the editor and Variables view on the primary monitor
- Drag Memory or Disassembly views to additional space as needed

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Shift+R` | Open Resource (quick file finder) |
| `F3` | Open Declaration (jump to symbol definition) |
| `Ctrl+O` | Quick Outline (navigate current file) |
| `F11` | Debug (launch last debug configuration) |
| `Ctrl+F11` | Run (launch last run configuration) |
| `F5` | Step Into (during debugging) |
| `F6` | Step Over (during debugging) |
| `F7` | Step Return (during debugging) |
| `F8` | Resume execution (during debugging) |

---

## Summary

| Concept | What It Is | How You Use It |
|---------|-----------|----------------|
| **Editor** | Content editing window | Double-click files to open; write and modify code |
| **View** | Information panel | Navigate, inspect, and control IDE features |
| **Perspective** | Task-oriented layout | Switch between C/C++, Debug, and QNX System layouts |
| **Window Menu** | Access to perspectives and views | `Window → Perspective` or `Window → Show View` |

Mastering these Eclipse fundamentals enables efficient navigation of the QNX Momentics IDE. The perspective-based workflow keeps your workspace organized by task, while flexible view management ensures you always have the right information visible.

---


*Happy coding!* 🚀
