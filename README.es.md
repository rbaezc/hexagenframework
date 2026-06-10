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
*   `sqlite` (Planificado): Base de datos relacional integrada en el binario.
*   `postgres` / `mysql` (Planificado): Conectores para bases de datos relacionales en red.

### Cómo Configurar
1.  **Define el motor en tu archivo `.hx`:**
    ```prolog
    config {
        database: postgres
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
    Si el servidor compilado no encuentra credenciales de conexión en el sistema o archivo `.env`, retornará automáticamente a almacenamiento local basado en JSONL/SQLite para mantener el flujo de desarrollo ágil y sin dependencias externas.

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
*(No requiere dependencias externas, ni Node.js, ni bases de datos relacionales externas, ni servidores web adicionales).*

---

### Paso 5: Probar la Seguridad del API (Pillar 3)
Puedes probar las restricciones de seguridad del endpoint `/process` enviando una petición HTTP manual:

*   **Petición No Autorizada (Bloqueada):**
    ```bash
    curl -i -X POST http://localhost:8080/process -d '{"item":"Mouse", "cantidad": 5}'
    # Retorna: HTTP/1.1 401 Unauthorized
    ```
*   **Petición Autorizada (Exitosa):**
    ```bash
    curl -i -X POST http://localhost:8080/process \
         -H "Authorization: Bearer hexagen_token_123" \
         -d '{"item":"Mouse", "cantidad": 5}'
    # Retorna: HTTP/1.1 200 OK y ejecuta la lógica nativa en C++
    ```

---

## 🛠️ Funcionalidades Avanzadas

### 1. Multi-vista y Redirección Integrada
Hexagen admite múltiples bloques `view` en tu código fuente. Para navegar entre ellos de forma nativa en la UI, define un botón cuyo destino (`->`) sea el nombre de la otra vista:
```prolog
view Home {
    title "Inicio"
    button "Ver Panel" -> Dashboard
}

view Dashboard {
    title "Panel de Control"
    button "Volver" -> Home
}
```
El transpilador generará redirecciones JavaScript de alto rendimiento usando `window.location.href = '/<vista>';` para una navegación fluida.

### 2. Operaciones de Eliminación CRUD (DELETE)
El framework soporta rutas HTTP de tipo `DELETE` para borrar registros atómicamente de tu base de datos `.jsonl`:
```prolog
api Rest {
    secure route "/eliminar" DELETE -> Inventario.Eliminar
}
```
Cuando el frontend realice una solicitud DELETE con el identificador (el primer campo de tu slice), el servidor C++ localizará la línea correspondiente en el archivo `.jsonl`, la eliminará de forma segura y reescribirá la persistencia de forma atómica. Además, si hay una tabla renderizando dicho slice en tu vista, se añadirá automáticamente una columna con un botón de **Eliminar** interactivo.

---

## 🛠️ Compilación Manual (Solo para Desarrolladores del Framework)
Si deseas realizar modificaciones en el compilador de Hexagen o compilarlo tú mismo en lugar de usar los scripts de instalación rápida, asegúrate de tener Go instalado y corre:
```bash
make clean && make
```
Esto compilará:
1. El núcleo del parser C++ en el binario local `./hf_core`.
2. La CLI orquestadora de Go en el binario local `./hf` (que envuelve a `hf_core`).
