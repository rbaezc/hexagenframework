# ⚡ Hexagen Framework (hf)

[English Version](README.md) | **Versión en Español**

Hexagen es un meta-compilador y framework de desarrollo full-stack unificado escrito en C++17. Te permite declarar tu lógica de negocio (`slice`), tu interfaz de usuario (`view`) y tus endpoints de red (`api`) en un único archivo de código fuente `.hx` y compilarlo en un **único binario ejecutable de C++ autónomo con persistencia de base de datos local y servidor HTTP integrado**.

---

## 🚀 Instalación Rápida (Out of the Box)

Puedes instalar Hexagen de forma inmediata sin necesidad de compilarlo manualmente. Ejecuta el comando correspondiente a tu sistema operativo en la terminal:

### 🐧 Linux y 🍎 macOS
```bash
curl -fsSL https://raw.githubusercontent.com/rbaezc/hexagenframework/main/install.sh | bash
```

### 🪟 Windows (PowerShell)
```powershell
irm https://raw.githubusercontent.com/rbaezc/hexagenframework/main/install.ps1 | iex
```

*Una vez instalado, reinicia tu terminal y ejecuta `hf --help` para verificar la instalación.*

---

## 🏗️ Arquitectura y Pilares del Framework

Hexagen automatiza todo el pipeline de desarrollo web, ruteo y bases de datos a través de sus pilares fundamentales:

1. **🗄️ Persistencia de Datos Automática (Pillar 1):** Cada `slice` autogenera métodos nativos en C++ para leer y escribir de forma atómica y concurrente en una base de datos local estructurada (`db_<Slice>.jsonl`).
2. **🛡️ Capa de Seguridad Nivel API (Pillar 3):** Rutas decoradas con la palabra clave `secure` bloquean peticiones HTTP externas que no incluyan un token de acceso válido en su cabecera (`Authorization: Bearer <token>`), respondiendo automáticamente `HTTP 401 Unauthorized`.
3. **⚡ Dev Watcher Loop & Hot Compiling (Pillar 4):** El CLI vigila cambios en tus archivos `.hx`, transpila el código, compila el servidor en segundo plano y lo reinicia automáticamente en microsegundos.
4. **📊 Componentes de UI Reactivos (Pillar 5):** Soporte nativo para tablas dinámicas que leen directamente de las APIs internas en C++ y se actualizan de forma reactiva al registrar datos a través de formularios.

---

## 🗄️ Configuración de Base de Datos

Hexagen admite configurar tu motor de base de datos de forma global usando el bloque `config` al inicio de tu archivo `.hx`.

### Motores Soportados
*   `jsonl` (Por defecto): Archivo plano local JSON Lines. Ideal para portabilidad sin dependencias.
*   `sqlite`: Base de datos relacional SQLite integrada de forma local. Crea y mapea estructuras automáticamente.
*   `postgres` / `postgresql`: Conector para base de datos relacional PostgreSQL en red.
*   `mysql`: Conector para base de datos relacional MySQL / MariaDB en red.

### Cómo Configurar
1.  **Define el motor y el frontend en tu archivo `.hx`:**
    ```prolog
    config {
        database: postgres
        frontend: react     # Opcional: vanilla (por defecto) o react
        css: tailwind       # Opcional: vanilla (por defecto) o tailwind
    }
    
    slice Tareas {
        field desc: string
        field completada: bool
    }
    ```
2.  **Guarda tus credenciales de forma segura en un archivo `.env`:**
    Los accesos a la base de datos (host, usuario, contraseña, puerto, etc.) se cargan en tiempo de ejecución desde variables de entorno o desde un archivo local `.env` (el cual debe ser agregado a tu `.gitignore`):
    ```ini
    DB_HOST=localhost
    DB_PORT=5432
    DB_USER=mi_usuario
    DB_PASS=mi_contraseña_segura
    DB_NAME=vortex_db
    ```
3.  **Fallback Local en Desarrollo:**
    Si el servidor compilado no encuentra credenciales de conexión en el sistema o en el archivo `.env` en tiempo de ejecución, retornará automáticamente a almacenamiento local basado en JSONL para mantener el flujo de desarrollo ágil y sin dependencias.

### 🛠️ Requisitos de Compilación y Enlace
Al compilar archivos `.hx` usando un motor de base de datos distinto de `jsonl`, asegúrate de tener las cabeceras de desarrollo correspondientes instaladas en tu sistema. Hexagen enlazará automáticamente las bibliotecas correctas (`-lsqlite3`, `-lpq` o `-lmysqlclient`):
*   **PostgreSQL**: Requiere las cabeceras de desarrollo de `libpq`.
    *   Ubuntu/Debian: `sudo apt-get install libpq-dev`
    *   Fedora/RHEL: `sudo dnf install postgresql-devel`
    *   macOS: `brew install postgresql`
*   **MySQL/MariaDB**: Requiere las cabeceras de desarrollo de `mysqlclient`.
    *   Ubuntu/Debian: `sudo apt-get install default-libmysqlclient-dev`
    *   Fedora/RHEL: `sudo dnf install mysql-devel` (o `mariadb-devel`)
    *   macOS: `brew install mysql-client`
*   **SQLite**: Requiere las cabeceras de desarrollo de `sqlite3`.
    *   Ubuntu/Debian: `sudo apt-get install libsqlite3-dev`
    *   Fedora/RHEL: `sudo dnf install sqlite-devel`
    *   macOS: `brew install sqlite`

---

## 📂 Estructura de Directorios del Repositorio

Si clonas el repositorio del framework para contribuir o entender su funcionamiento interno, esta es la estructura:

```text
hexagen_framework/
├── build/                # Objetos compilados temporales de C++
├── cli/                  # Código fuente del wrapper de CLI en Go
│   └── main.go           # CLI Orquestadora (vigila archivos, gestiona procesos)
├── src/                  # Código fuente del núcleo del compilador en C++
│   ├── token.hpp         # Definición de tokens del lenguaje
│   ├── lexer.hpp/cpp     # Analizador Léxico
│   ├── ast.hpp           # Estructuras del Árbol de Sintaxis Abstracta
│   ├── parser.hpp/cpp    # Parser descendente recursivo
│   ├── codegen.hpp/cpp   # Transpilador C++ / Generador HTTP & HTML
│   └── main.cpp          # Punto de entrada de C++ core (crea hf_core)
├── Makefile              # Script de compilación híbrido (Go + C++)
├── CMakeLists.txt        # Configuración de CMake
├── demo.hx               # Código de prueba general-purpose (Inventario)
├── install.sh            # Script de instalación para Unix (instala hf y hf_core)
└── install.ps1           # Script de instalación para Windows (instala hf.exe y hf_core.exe)
```

---

## 🚀 Guía de Inicio Paso a Paso

### Paso 1: Crear tu Proyecto (`hf new`)
Puedes inicializar un espacio de trabajo listo para programar con un solo comando:
```bash
hf new mi_proyecto
```
Esto creará una carpeta llamada `mi_proyecto` con un archivo `app.hx` base y un `Makefile` configurado.

---

### Paso 2: Escribir tu Código Fuente (`app.hx`)
Escribe tu aplicación definiendo tu modelo, pantalla y enrutamiento. Aquí tienes el ejemplo de inventarios ([demo.hx](demo.hx)):
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

### Paso 3: Correr en Modo de Desarrollo (Hot Reload)
Para trabajar de manera interactiva con actualizaciones automáticas en segundo plano, corre:
```bash
hf dev demo.hx
```
* Esto compilará y levantará la aplicación en **[http://localhost:8080](http://localhost:8080)**.
* Si modificas y guardas `demo.hx`, **`hf` recompilará el servidor de C++ en caliente al instante**.

---

### Paso 4: Compilar a Producción (Binario Standalone)
Cuando tu aplicación esté lista, puedes empaquetarla en un ejecutable monolítico ultra-rápido:
```bash
hf compile demo.hx -o mi_servidor
```
Esto genera el ejecutable `mi_servidor`. Para correrlo en cualquier máquina de producción, basta con ejecutar:
```bash
./mi_servidor
```

---

### Paso 5: Contenedorizar para Producción (Docker & Docker Compose)
Hexagen puede generar automáticamente configuraciones de contenedores optimizadas y adaptadas al motor de base de datos de tu proyecto:
```bash
hf dockerize .
```
Esto genera:
1. Un `Dockerfile` multietapa optimizado para caché que compila tu aplicación de C++ dentro de un entorno seguro y genera una imagen final basada en `debian-slim` mínima.
2. Un archivo `docker-compose.yml` listo para producción, preconfigurado con los contenedores de bases de datos correctos (Postgres, MySQL) y mapeos de volúmenes persistentes (para JSONL, SQLite y almacenamiento de archivos subidos).

Para arrancar tu entorno de producción, simplemente ejecuta:
```bash
docker-compose up --build -d
```

---

### Paso 6: Probar la Seguridad del API (Pillar 3)
Puedes probar las restricciones de seguridad de las rutas seguras (anotadas con `secure`) enviando peticiones HTTP:

*   **Petición No Autorizada (Bloqueada):**
    ```bash
    curl -i -X POST http://localhost:8080/process -d '{"item":"Mouse", "cantidad": 5}'
    # Retorna: HTTP/1.1 401 Unauthorized
    ```
*   **Registrar un Usuario:**
    ```bash
    curl -i -X POST http://localhost:8080/api/signup \
         -d '{"email":"test@example.com", "password":"secret_password", "rol":"admin"}'
    # Retorna: HTTP/1.1 201 Created
    ```
*   **Iniciar sesión para obtener un token firmado criptográficamente:**
    ```bash
    curl -i -X POST http://localhost:8080/api/login \
         -d '{"email":"test@example.com", "password":"secret_password"}'
    # Retorna: HTTP/1.1 200 OK y tu token de sesión dinámico
    ```
*   **Petición Autorizada (usando el token firmado):**
    ```bash
    curl -i -X POST http://localhost:8080/process \
         -H "Authorization: Bearer <TU_TOKEN_DE_SESION>" \
         -d '{"item":"Mouse", "cantidad": 5}'
    # Retorna: HTTP/1.1 200 OK y ejecuta la lógica nativa en C++
    ```

---

### Paso 7: Migraciones de Esquema de Base de Datos (Pilar 2 / Fase 2)
Hexagen cuenta con un motor de migraciones de base de datos integrado que compara tus archivos locales `.hx` con tu última captura del esquema para generar scripts de migración SQL incrementales (`.up.sql` y `.down.sql`) y aplicarlos de forma segura:

*   **Generar una migración incremental (diff):**
    ```bash
    hf db diff .
    ```
    Compara tus archivos `.hx` actuales con `db/schema.json` y genera scripts de migración con marca de tiempo en `db/migrations/` (por ejemplo, `20260612122544_migration.up.sql` y `20260612122544_migration.down.sql`).
*   **Ejecutar migraciones pendientes:**
    ```bash
    hf db migrate .
    ```
    Compila en caliente una herramienta de migración de base de datos en C++, se conecta a tu base de datos y ejecuta todas las migraciones no aplicadas secuencialmente.
*   **Revertir la última migración (rollback):**
    ```bash
    hf db rollback .
    ```
    Aplica los comandos del archivo `.down.sql` correspondientes a la última versión de migración aplicada y actualiza el historial de seguimiento.

---

## 🛠️ Funcionalidades Avanzadas

### 1. Multi-vista y Redirección Integrada
Hexagen admite múltiples bloques `view`. Para navegar:
```prolog
view Home {
    title "Inicio"
    button "Ver Panel" -> Dashboard
}
```
El transpilador generará redirecciones JavaScript de alto rendimiento usando `window.location.href = '/<vista>';`.

### 2. Operaciones de Eliminación CRUD (DELETE)
El framework soporta rutas HTTP `DELETE` para borrar de forma atómica registros de tu base de datos `.jsonl`:
```prolog
api Rest {
    secure route "/eliminar" DELETE -> Inventario.Eliminar
}
```
Cuando el frontend realiza una solicitud DELETE con el identificador, el servidor C++ localiza la línea, la elimina de forma segura y reescribe la persistencia. Se añade automáticamente una columna con un botón interactivo **Eliminar**.

### 3. Parámetros de Consulta, Filtrado y Paginación
Hexagen expone automáticamente capacidades de filtrado dinámico y paginación en todos los endpoints GET auto-generados para los slices (`/api/<NombreSlice>`). Puedes enviar cadenas de consulta (query strings) para realizar consultas parametrizadas seguras:

*   **Filtrado de Campos:** Filtra resultados haciendo coincidir campos de los slices.
    ```bash
    curl -i http://localhost:8080/api/Plato?categoria_id=3&disponible=true
    ```
*   **Paginación:** Limita y desplaza los resultados utilizando los parámetros de consulta `_limit` y `_offset`.
    ```bash
    curl -i http://localhost:8080/api/Plato?_limit=10&_offset=20
    ```

Esto funciona de forma predeterminada en todos los motores compatibles (JSONL, SQLite y PostgreSQL/MySQL), implementando comprobaciones en tiempo de ejecución optimizadas y consultas parametrizadas para evitar inyecciones SQL.

*   **Autenticación Dinámica de Usuarios y Tokens de Sesión Criptográficos:**
    Si se declara un slice que representa usuarios (llamado exactamente `Usuario` o `User`) en tu código Hexagen (con un campo email/correo/username y un campo contrasena/password/clave), Hexagen expone automáticamente:
    *   **`POST /api/signup`**: Registro estándar de usuarios. Aplica un hash seguro a la contraseña usando SHA-256 antes de persistirla en la base de datos.
    *   **`POST /api/login`**: Autenticación de usuarios. Si las credenciales son correctas, genera y devuelve un token de sesión inmune a manipulaciones.
    
    **Esquema de Tokens de Sesión:**
    Para mantener una arquitectura libre de dependencias (evitando librerías externas pesadas de criptografía como OpenSSL), los tokens de sesión se generan bajo el siguiente esquema seguro:
    `Base64URL(payload) + "." + SHA256(Base64URL(payload) + "." + secreto)`
    
    **Configuración:**
    La firma de validación utiliza una clave secreta cargada desde tu archivo `.env` mediante la variable `JWT_SECRET`. Si no se configura ningún secreto, se utiliza un valor por defecto.


### 4. Middlewares de API y Trabajos en Segundo Plano Asíncronos
Hexagen ofrece soporte nativo para directivas de middleware a nivel de API y procesamiento asíncrono en segundo plano.

#### Middlewares de API
Dentro de un bloque `api`, puedes registrar middlewares globales usando la directiva `use`:
*   **CORS (`use cors`):** Maneja de manera nativa las solicitudes preflight `OPTIONS`, inyectando las cabeceras estándar de acceso cruzado (`Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods: *`, etc.) en todos los endpoints dinámicos.
*   **Limitación de Tasa Basada en IP (`use rate_limit(limit, window)`):** Rastrea las direcciones IP de los clientes mediante mapeos locales seguros para hilos (sin requerir dependencias externas como Redis) y bloquea a los clientes con una respuesta `429 Too Many Requests` cuando se excede el presupuesto de solicitudes en la ventana de tiempo especificada (en segundos).

```prolog
api MiApi {
    use cors
    use rate_limit(100, 60) // Límite de 100 solicitudes por cada 60 segundos

    route "/items" GET -> Inventario.Listar
}
```

#### Trabajos Asíncronos en Segundo Plano (Background Jobs)
Los trabajos en segundo plano permiten ejecutar tareas costosas o prolongadas (como enviar correos electrónicos, registrar auditorías o procesar imágenes) de forma asíncrona mediante un grupo de hilos de trabajo, evitando bloquear el bucle de eventos principal del servidor web.
*   **Sintaxis de Trabajo (Job):** Se define con el bloque `job`, soportando campos (fields) y una acción `Run`.
*   **Sintaxis de Encolado (Enqueue):** Se encola una tarea de forma asíncrona usando `enqueue JobName(field1: val1, field2: val2)`.

```prolog
job EnviarNotificacion {
    field usuarioId: int
    field mensaje: string

    action Run() {
        print("Enviando mensaje al usuario: " + mensaje)
    }
}

slice Pedido {
    field pedidoId: int

    action Crear() {
        print("Creando pedido")
        enqueue EnviarNotificacion(usuarioId: 1, mensaje: "¡Tu pedido ha sido creado!")
    }
}
```

Hexagen transpila los trabajos en estructuras C++ y arranca automáticamente un grupo de hilos de trabajo seguros (`std::thread`, `std::mutex`, `std::condition_variable`) al iniciar el servidor para consumir y ejecutar las tareas en segundo plano.

### 5. Subida de Archivos Multipart y Servidor Estático
Hexagen admite de forma nativa la subida de archivos tipo `multipart/form-data` para manejar archivos multimedia (como fotos de platos de comida en una API de restaurante).

* **Cómo funciona:** Cuando se envía una solicitud POST con `Content-Type: multipart/form-data` a una ruta de acción de un slice, el parser multipart incorporado de Hexagen procesa automáticamente los campos del archivo, los guarda localmente en el directorio `public/uploads/` con un prefijo de timestamp único (para evitar colisiones de nombres), y escribe la ruta del archivo (por ejemplo, `/public/uploads/1715694200_dish.png`) directamente en el registro correspondiente de la base de datos.
* **Servidor Estático:** El servidor compilado actúa automáticamente como un servidor de archivos estáticos de alto rendimiento bajo la ruta `/public/uploads/*`, detectando y asignando las cabeceras MIME correctas para imágenes (`image/png`, `image/jpeg`, `image/gif`) de forma dinámica.

### 6. WebSockets para Tiempo Real
Se admite de forma nativa la sincronización bidireccional en tiempo real utilizando el protocolo WebSocket:
* **Sintaxis:**
  ```prolog
  api RealtimeNotifier {
      websocket "/pedidos-realtime" -> Cocina.NuevaOrden
  }
  ```
* **Handshake y Protocolo:** Hexagen implementa el handshake de WebSockets (RFC 6455) de forma nativa en C++ utilizando rutinas personalizadas de SHA-1 y Base64. Decodifica los frames de texto entrantes de cliente a servidor (gestionando el enmascaramiento XOR) y codifica los frames de servidor a cliente sin depender de librerías de red externas.
* **Sincronización y Difusión (Broadcast):**
  * Las conexiones activas de los clientes se mantienen en un pool seguro para subprocesos (thread-safe).
  * Cada vez que un cliente envía datos a través de un WebSocket, Hexagen parsea la carga útil JSON, rellena los campos de la acción del slice, los guarda en la base de datos, ejecuta la acción en C++ y difunde los datos recibidos a todas las demás conexiones WebSocket activas.
  * **Difusión de HTTP a WebSocket:** Para permitir paneles de usuario en tiempo real, cada vez que se activa *cualquier* endpoint de acción HTTP POST estándar (por ejemplo, al crear un nuevo pedido), el servidor difunde automáticamente un evento de notificación `{"event": "action", "target": "Slice.Action"}` a todos los clientes WebSocket conectados.

### 7. Analizador Estático de Seguridad y Compilador Sandbox
Para proteger el ecosistema de Hexagen contra contribuciones maliciosas (como ataques de cadena de suministro al estilo NPM/XZ), el compilador incluye un **Inspector de Seguridad** integrado que analiza el código en dos fases:

* **1. Comprobaciones de Seguridad Léxicas (Antes del Parsing):**
  * **Anti-Ofuscación:** Escanea en busca de cargas útiles ofuscadas (como bloques en base64 o hexadecimal) marcando cadenas literales de más de 200 caracteres sin espacios.
  * **Anti-Shellcode:** Detecta y bloquea arrays gigantes de enteros (más de 50 números consecutivos separados únicamente por comas) utilizados comúnmente para inyectar cargas binarias crudas.
  * **Sandbox del Sistema:** Bloquea operaciones directas de memoria y sockets de red no autorizados. El uso de identificadores como `malloc`, `free`, `socket`, `connect`, `pthread_create`, `fork`, `execve` o punteros directos (sintaxis `*` utilizada para manipulación de direcciones) está estrictamente prohibido y abortará la compilación.
* **2. Análisis de Flujo de Datos y Taint Analysis en el AST (Después del Parsing):**
  * **Anti-Inyección de Comandos:** Audita los argumentos de llamadas a funciones que ejecutan comandos del sistema (`system`, `popen`, `exec`). El inspector asegura que los argumentos sean cadenas literales estáticas y nunca variables provenientes de peticiones HTTP de clientes.
  * **Anti-Filtración de Credenciales:** Rastreará variables sensibles (tales como campos de configuración de entorno o variables que contengan `pass`, `secret`, `key`, `token`, `auth`, `cred` en sus nombres). Si una variable marcada (tainted) se pasa a una función de exfiltración (como `curl`, `fetch`, `send` o se imprime en la consola), la compilación se rechaza de inmediato.

### 8. Andamiaje Moderno React + Tailwind CSS y Alojamiento Estático SPA
Si especificas `frontend: react` y `css: tailwind` en el bloque `config` de tu archivo `.hx`, Hexagen compila tus vistas de interfaz de usuario en una aplicación de página única (SPA) moderna basada en React + TypeScript:

* **Andamiaje Automático (Auto-Scaffolding):** En la primera transpilación/compilación, Hexagen crea un proyecto completo Vite + React + TypeScript + Tailwind CSS dentro de una carpeta `frontend/` (incluyendo `package.json`, `tsconfig.json`, `vite.config.ts`, y todas las configuraciones de estilos).
* **Compilador de Vista a Componente:** Convierte cada bloque `view` de `.hx` en un componente de página de React individual bajo `frontend/src/pages/` (generando automáticamente variables de estado para inputs y tablas, manejadores de eventos dinámicos para llamadas a acciones a través de botones, enlaces de navegación y tablas dinámicas estilizadas con soporte para acciones de eliminación/CRUD).
* **Servidor C++ de Alto Rendimiento para Alojamiento SPA:** El servidor web compilado en C++ aloja la SPA de forma nativa. Sirve los archivos estáticos empaquetados desde `frontend/dist/` asociando las cabeceras de tipo MIME correspondientes. Para cualquier petición de rutas que no pertenezcan al API o websocket (e.g. `/inventario`), el servidor redirige el flujo de control a `frontend/dist/index.html` permitiendo que `react-router-dom` maneje la navegación del lado del cliente, mientras continúa enrutando `/api/*` y WebSockets a los controladores C++.
* **Optimización y Caché de Dependencias (node_modules):** El transpilador evita instalaciones repetitivas y lentas verificando si la carpeta `frontend/node_modules/` ya existe, logrando compilaciones incrementales con Vite en menos de dos segundos.

### 9. Integración con Editores: LSP y Extensión para VS Code
Hexagen provee soporte profesional para entornos de desarrollo utilizando el Language Server Protocol (LSP) y una extensión dedicada para VS Code (en la carpeta `vscode-extension/`):

* **Comando del Language Server (`hf lsp`):** Inicia un servidor de lenguaje nativo escrito en Go que procesa mensajes JSON-RPC a través de la entrada y salida estándar (stdin/stdout).
* **Validación y Diagnósticos en Tiempo Real:** Analiza los archivos `.hx` al abrir y modificar el documento (`didOpen`/`didChange`), ejecutando comprobaciones léxicas y análisis de seguridad basados en AST. Si se detecta un error de sintaxis o seguridad, lo reporta de forma visual en la línea exacta del código en tu editor.
* **Autocompletado e IntelliSense:** Sugiere dinámicamente palabras clave de Hexagen, tipos de datos primitivos, slices definidos por el usuario, y acciones de slices (e.g. `Tareas.Crear`) contextualmente según el lugar donde escribes.
* **Resaltado de Sintaxis Rico:** Incluye una gramática TextMate personalizada (`hexagen.tmLanguage.json`) para colorear palabras clave, bloques, cadenas de texto, comentarios y tipos primitivos de manera limpia.

---

## 🛠️ Compilación Manual (Solo para Desarrolladores del Framework)
Si deseas realizar modificaciones en el compilador de Hexagen o compilarlo tú mismo en lugar de usar los scripts de instalación rápida, asegúrate de tener Go instalado y corre:
```bash
make clean && make
```
Esto compilará:
1. El núcleo del parser C++ en el binario local `./hf_core`.
2. La CLI orquestadora de Go en el binario local `./hf` (que envuelve a `hf_core`).
