# ⚡ Hexagen Framework (hf)

**English Version** | [Versión en Español](README.es.md)

Hexagen is a unified full-stack development framework and meta-compiler written in C++17. It allows you to declare your business logic (`slice`), your user interface (`view`), and your network endpoints (`api`) in a single `.hx` source file and compile them into a **single, standalone C++ executable binary with local database persistence and an integrated HTTP server**.

---

## 🚀 Quick Installation (Out of the Box)

You can install Hexagen immediately without needing to compile it manually. Run the command corresponding to your operating system in your terminal:

### 🐧 Linux & 🍎 macOS
```bash
curl -fsSL https://raw.githubusercontent.com/rbaezc/hexagenframework/main/install.sh | bash
```

### 🪟 Windows (PowerShell)
```powershell
irm https://raw.githubusercontent.com/rbaezc/hexagenframework/main/install.ps1 | iex
```

*Once installed, restart your terminal and run `hf --help` to verify the installation.*

---

## 🏗️ Architecture and Framework Pillars

Hexagen automates the entire web development pipeline, routing, and database layer through its key pillars:

1. **🗄️ Automatic Data Persistence (Pillar 1):** Each `slice` auto-generates native C++ methods to read and write atomically and concurrently in a structured local database file (`db_<Slice>.jsonl`).
2. **🛡️ API-Level Security Layer (Pillar 3):** Routes decorated with the `secure` keyword block external HTTP requests that do not include a valid access token in their header (`Authorization: Bearer <token>`), automatically responding with `HTTP 401 Unauthorized`.
3. **⚡ Dev Watcher Loop & Hot Compiling (Pillar 4):** The CLI watches for changes in your `.hx` files, transpiles the code, compiles the server in the background, and restarts it automatically in microseconds.
4. **📊 Reactive UI Components (Pillar 5):** Native support for dynamic tables that read directly from internal C++ APIs and update reactively when data is registered through forms.

---

## 📂 Repository Directory Structure

If you clone the framework repository to contribute or understand its inner workings, this is the directory structure:

```text
hexagen_framework/
├── build/                # Temporary compiled C++ objects
├── cli/                  # Go CLI wrapper source code
│   └── main.go           # Orchestrator CLI (watches files, manages processes)
├── src/                  # Source code for the hf C++ parser core
│   ├── token.hpp         # Definition of language tokens
│   ├── lexer.hpp/cpp     # Hand-written Lexical Analyzer
│   ├── ast.hpp           # Abstract Syntax Tree structures
│   ├── parser.hpp/cpp    # Recursive descent parser
│   ├── codegen.hpp/cpp   # C++ Transpiler / HTTP & HTML generator
│   └── main.cpp          # C++ core CLI entry point (creates hf_core)
├── Makefile              # Hybrid compilation script (Go + C++)
├── CMakeLists.txt        # CMake configuration
├── demo.hx               # General-purpose test code (Inventory)
├── install.sh            # Unix installation script (installs hf and hf_core)
└── install.ps1           # Windows installation script (installs hf.exe and hf_core.exe)
```

---

## 🚀 Step-by-Step Getting Started Guide

### Step 1: Create Your Project (`hf new`)
You can initialize a project workspace ready to code with a single command:
```bash
hf new my_project
```
This will create a folder named `my_project` containing a base `app.hx` file and a configured `Makefile`.

---

### Step 2: Write Your Source Code (`app.hx`)
Write your application by defining your model, screen, and routing. Here is the inventory example ([demo.hx](demo.hx)):
```prolog
slice Inventario {
    field item: string
    field cantidad: int
    
    action Agregar() {
        print("Registrando producto:")
        print(item)
    }
}

view Main {
    title "Vortex Control de Inventario"
    input item: string
    input cantidad: int
    button "Guardar Producto en C++" -> Inventario.Agregar
    table Inventario -> item, cantidad
}

api Rest {
    secure route "/process" POST -> Inventario.Agregar
}
```

---

### Step 3: Run in Development Mode (Hot Reload)
To work interactively with automatic updates running in the background, run:
```bash
hf dev demo.hx
```
* This compiles and serves the application at **[http://localhost:8080](http://localhost:8080)**.
* If you modify and save `demo.hx`, **`hf` will hot-recompile the C++ server instantly**.

---

### Step 4: Compile for Production (Standalone Binary)
When your application is ready, you can package it into an ultra-fast, monolithic executable:
```bash
hf compile demo.hx -o my_server
```
This generates the `my_server` executable. To run it on any production machine, simply execute:
```bash
./my_server
```
*(Requires no external dependencies, no Node.js, no external relational databases, and no additional web servers).*

---

### Step 5: Test API Security (Pillar 3)
You can test the security restrictions of the `/process` endpoint by sending a manual HTTP request:

*   **Unauthorized Request (Blocked):**
    ```bash
    curl -i -X POST http://localhost:8080/process -d '{"item":"Mouse", "cantidad": 5}'
    # Returns: HTTP/1.1 401 Unauthorized
    ```
*   **Authorized Request (Successful):**
    ```bash
    curl -i -X POST http://localhost:8080/process \
         -H "Authorization: Bearer hexagen_token_123" \
         -d '{"item":"Mouse", "cantidad": 5}'
    # Returns: HTTP/1.1 200 OK and executes native C++ logic
    ```

---

## 🛠️ Advanced Features

### 1. Multi-view and Built-in Redirection
Hexagen supports multiple `view` blocks in your source code. To navigate between them natively in the UI, define a button whose target (`->`) is the name of the other view:
```prolog
view Home {
    title "Home"
    button "View Dashboard" -> Dashboard
}

view Dashboard {
    title "Control Dashboard"
    button "Go Back" -> Home
}
```
The transpiler will generate high-performance JavaScript redirects using `window.location.href = '/<view>';` for fluid navigation.

### 2. CRUD Delete Operations (DELETE)
The framework supports `DELETE` HTTP routes to atomically erase records from your `.jsonl` database:
```prolog
api Rest {
    secure route "/eliminar" DELETE -> Inventario.Eliminar
}
```
When the frontend makes a DELETE request with the identifier (the first field of your slice), the C++ server locates the corresponding line in the `.jsonl` file, safely removes it, and rewrites the persistent store atomically. Additionally, if a table renders that slice in your view, an interactive **Delete** button column will automatically be appended.

---

## 🛠️ Manual Compilation (Framework Contributors Only)
If you want to modify the Hexagen compiler or compile it yourself instead of using the quick installation scripts, make sure you have Go installed and run:
```bash
make clean && make
```
This will compile:
1. The C++ parser core into the local `./hf_core` binary.
2. The Go CLI orchestrator into the local `./hf` binary wrapper.
