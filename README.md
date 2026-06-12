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

## 🗄️ Database Configuration

Hexagen supports configuring your database engine globally using the `config` block at the beginning of your `.hx` file. 

### Supported Engines
*   `jsonl` (Default): Local plain-text JSON Lines file. Great for zero-dependency portability.
*   `sqlite`: Local embedded SQLite relational database. Automatically creates/maps table structures.
*   `postgres` / `postgresql`: Remote PostgreSQL relational database engine.
*   `mysql`: Remote MySQL / MariaDB relational database engine.

### How to Configure
1.  **Define the Engine in your `.hx` file:**
    ```prolog
    config {
        database: postgres
    }
    
    slice Tareas {
        field desc: string
        field completada: bool
    }
    ```
2.  **Store Credentials Securely in a `.env` file:**
    Database credentials (hosts, usernames, passwords) are loaded at runtime from environment variables or a local `.env` file (which should be added to `.gitignore`):
    ```ini
    DB_HOST=localhost
    DB_PORT=5432
    DB_USER=my_user
    DB_PASS=my_password
    DB_NAME=vortex_db
    ```
3.  **Local Dev Fallback:**
    If no database credentials are found in the system or `.env` file at runtime, the compiled server automatically falls back to local JSONL storage to keep development frictionless and out-of-the-box.

### 🛠️ Compilation and Linker Requirements
When compiling `.hx` files using a database engine other than `jsonl`, ensure you have the corresponding development headers installed on your system. Hexagen will automatically link the correct libraries (`-lsqlite3`, `-lpq`, or `-lmysqlclient`):
*   **PostgreSQL**: Requires `libpq` development headers.
    *   Ubuntu/Debian: `sudo apt-get install libpq-dev`
    *   Fedora/RHEL: `sudo dnf install postgresql-devel`
    *   macOS: `brew install postgresql`
*   **MySQL/MariaDB**: Requires `mysqlclient` development headers.
    *   Ubuntu/Debian: `sudo apt-get install default-libmysqlclient-dev`
    *   Fedora/RHEL: `sudo dnf install mysql-devel` (or `mariadb-devel`)
    *   macOS: `brew install mysql-client`
*   **SQLite**: Requires `sqlite3` development headers.
    *   Ubuntu/Debian: `sudo apt-get install libsqlite3-dev`
    *   Fedora/RHEL: `sudo dnf install sqlite-devel`
    *   macOS: `brew install sqlite`

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

### Step 5: Containerize for Production (Docker & Docker Compose)
Hexagen can automatically generate optimized container configurations tailored to your project's database engine:
```bash
hf dockerize .
```
This generates:
1. A multi-stage, caching-friendly `Dockerfile` that compiles your C++ application inside a secure environment and targets a minimal `debian-slim` base image.
2. A production-ready `docker-compose.yml` pre-configured with the correct database containers (PostgreSQL, MySQL) and volume mounts for persistence (JSONL, SQLite, and upload directories).

To spin up your production stack, simply run:
```bash
docker-compose up --build -d
```

---

### Step 6: Test API Security (Pillar 3)
You can test the security restrictions of secure routes (annotated with `secure`) by sending HTTP requests:

*   **Unauthorized Request (Blocked):**
    ```bash
    curl -i -X POST http://localhost:8080/process -d '{"item":"Mouse", "cantidad": 5}'
    # Returns: HTTP/1.1 401 Unauthorized
    ```
*   **Sign up a User:**
    ```bash
    curl -i -X POST http://localhost:8080/api/signup \
         -d '{"email":"test@example.com", "password":"secret_password", "rol":"admin"}'
    # Returns: HTTP/1.1 201 Created
    ```
*   **Log in to obtain a cryptographically signed token:**
    ```bash
    curl -i -X POST http://localhost:8080/api/login \
         -d '{"email":"test@example.com", "password":"secret_password"}'
    # Returns: HTTP/1.1 200 OK and your dynamic session token
    ```
*   **Authorized Request (using the signed token):**
    ```bash
    curl -i -X POST http://localhost:8080/process \
         -H "Authorization: Bearer <YOUR_SESSION_TOKEN>" \
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

### 3. Query Parameters, Filtering, and Pagination
Hexagen automatically exposes dynamic query filtering and pagination on all auto-generated slice GET endpoints (`/api/<SliceName>`). You can send query strings to perform safe, parameterized queries:

*   **Field Filtering:** Filter results by matching slice fields.
    ```bash
    curl -i http://localhost:8080/api/Plato?categoria_id=3&disponible=true
    ```
*   **Pagination:** Limit and offset the results using `_limit` and `_offset` query parameters.
    ```bash
    curl -i http://localhost:8080/api/Plato?_limit=10&_offset=20
    ```

This works out of the box across all supported engines (JSONL, SQLite, and PostgreSQL/MySQL), implementing optimized runtime checks and parameterized queries to prevent SQL injections.

*   **Dynamic User Authentication & Cryptographic Session Tokens:**
    If a slice representing users (named exactly `Usuario` or `User`) is declared in your Hexagen code (with an email/correo/username field and a contrasena/password/clave field), Hexagen automatically exposes:
    *   **`POST /api/signup`**: Standard user registration. It hashes the password securely using SHA-256 before persisting it to the database.
    *   **`POST /api/login`**: User authentication. If credentials are correct, it generates and returns a tamper-proof session token.
    
    **Session Token Schema:**
    To preserve a zero-dependency architecture (avoiding heavy external cryptographical bindings like OpenSSL), session tokens are generated using the following secure schema:
    `Base64URL(payload) + "." + SHA256(Base64URL(payload) + "." + secret)`
    
    **Configuration:**
    The validation signature uses a secret key loaded from your `.env` file via `JWT_SECRET`. If no secret is configured, a default fallback is used.

### 5. Multipart File Uploads & Static File Serving
Hexagen natively supports `multipart/form-data` uploads for handling media (such as food dish photos in a restaurant API). 

* **How it works:** When a POST request with `Content-Type: multipart/form-data` is sent to a slice action route, Hexagen's built-in multipart parser automatically processes the file fields, saves them locally inside the `public/uploads/` directory with a unique timestamp prefix (to prevent name collisions), and writes the file's path (e.g. `/public/uploads/1715694200_dish.png`) directly to the slice database record.
* **Static Serving:** The compiled server automatically acts as a high-performance static file server under the `/public/uploads/*` route, detecting and assigning correct image MIME headers (`image/png`, `image/jpeg`, `image/gif`) dynamically.

### 6. WebSockets for Real-Time Notifications
Bidirectional real-time sync is natively supported using the WebSocket protocol:
* **Syntax:**
  ```prolog
  api RealtimeNotifier {
      websocket "/pedidos-realtime" -> Cocina.NuevaOrden
  }
  ```
* **Handshake & Protocol:** Hexagen implements the RFC 6455 WebSocket handshake natively in C++ using custom SHA-1 and Base64 routines. It decodes incoming client-to-server text frames (managing XOR masking) and encodes server-to-client frames without external networking libraries.
* **Sync & Broadcast:** 
  * Active client connections are kept in a thread-safe pool.
  * Whenever a client sends data over a WebSocket, Hexagen parses the JSON payload, populates the action's slice fields, saves it to the database, executes the C++ slice action, and broadcasts the client's payload to all other active WebSocket connections.
  * **HTTP -> WebSocket Broadcast:** To enable real-time UI dashboards, whenever *any* standard HTTP POST action endpoint is triggered (e.g., creating a new order), the server automatically broadcasts an event notification payload `{"event": "action", "target": "Slice.Action"}` to all connected WebSocket clients.

### 7. Static Security Analyzer & Sandbox Compiler
To protect the Hexagen ecosystem against malicious contributions (like NPM/XZ-style supply chain attacks), the compiler includes an integrated **Security Inspector** that analyzes code in two phases:

* **1. Lexical Security Checks (Before Parsing):**
  * **Anti-Obfuscation:** Scans for obfuscated payloads (such as base64/hex blobs) by flagging string literals longer than 200 characters without spaces.
  * **Anti-Shellcode:** Flags giant arrays of integers (>50 consecutive numbers separated only by commas) used to inject raw binary payloads.
  * **System Sandbox:** Blocks direct memory operations and unauthorized network socket operations. Identifiers like `malloc`, `free`, `socket`, `connect`, `pthread_create`, `fork`, `execve`, or raw pointers (`*` syntax used for address manipulation) are forbidden and will abort compilation.
* **2. AST Data Flow Taint Analysis (After Parsing):**
  * **Anti-Command Injection:** Audits call arguments for functions executing system commands (`system`, `popen`, `exec`). The inspector ensures that arguments are static literal strings and never variables sourced from client HTTP requests.
  * **Anti-Credential Leak:** Tracks sensitive variables (such as environment configuration fields or variables containing `pass`, `secret`, `key`, `token`, `auth`, `cred` in their names). If a tainted variable is passed to an exfiltration function (such as `curl`, `fetch`, `send`, or printed to console), the build is immediately rejected.

---

## 🛠️ Manual Compilation (Framework Contributors Only)
If you want to modify the Hexagen compiler or compile it yourself instead of using the quick installation scripts, make sure you have Go installed and run:
```bash
make clean && make
```
This will compile:
1. The C++ parser core into the local `./hf_core` binary.
2. The Go CLI orchestrator into the local `./hf` binary wrapper.
