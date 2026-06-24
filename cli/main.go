package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

const Version = "v2.2.0"

func printUsage() {
	fmt.Printf("Hexagen Framework CLI (hf) %s\n", Version)
	fmt.Println("Usage:")
	fmt.Println("  hf new <project_name>               - Scaffold a new project workspace")
	fmt.Println("  hf dev [file_or_dir]                - Start live dev server (Hot Reload) (default: .)")
	fmt.Println("  hf start [file_or_dir]              - Compile and run in production mode (default: .)")
	fmt.Println("  hf transpile [file_or_dir]          - Transpile to C++ source code (default: .)")
	fmt.Println("  hf compile [file_or_dir] -o <out>   - Transpile and compile to executable (default: .)")
	fmt.Println("  hf run [file_or_dir]                - Transpile, compile, and run immediately (default: .)")
	fmt.Println("  hf ast [file_or_dir]                - Print the AST of the input source (default: .)")
	fmt.Println("  hf dockerize [file_or_dir]          - Generate production-ready Dockerfile & docker-compose.yml (default: .)")
	fmt.Println("  hf db diff [file_or_dir]            - Generate an incremental SQL migration file (default: .)")
	fmt.Println("  hf db migrate [file_or_dir]         - Run all pending SQL migrations (default: .)")
	fmt.Println("  hf db rollback [file_or_dir]        - Rollback the last applied SQL migration (default: .)")
	fmt.Println("  hf lsp                              - Start Hexagen Language Server (LSP) for editor integration")
	fmt.Println("  hf help                             - Show this help message")
}

// Helper to find the hf_core compiler binary
func findCoreBinary() (string, error) {
	// 1. Check same directory as the running hf executable
	exePath, err := os.Executable()
	if err == nil {
		dir := filepath.Dir(exePath)
		corePath := filepath.Join(dir, "hf_core")
		if _, err := os.Stat(corePath); err == nil {
			return corePath, nil
		}
	}

	// 2. Check current working directory
	if _, err := os.Stat("./hf_core"); err == nil {
		return "./hf_core", nil
	}

	// 3. Check in system PATH
	path, err := exec.LookPath("hf_core")
	if err == nil {
		return path, nil
	}

	return "", fmt.Errorf("could not find 'hf_core' binary. Make sure it is compiled and in the same directory or PATH")
}

// Helper to run hf_core and return stdout
func runCore(args ...string) (string, error) {
	corePath, err := findCoreBinary()
	if err != nil {
		return "", err
	}

	cmd := exec.Command(corePath, args...)
	cmd.Stderr = os.Stderr
	output, err := cmd.Output()
	if err != nil {
		return string(output), fmt.Errorf("hf_core execution failed: %v", err)
	}
	return string(output), nil
}

// Scaffolds a new project
func handleNew(projectName string) {
	if _, err := os.Stat(projectName); err == nil {
		fmt.Printf("❌ Error: Directory '%s' already exists.\n", projectName)
		os.Exit(1)
	}

	err := os.Mkdir(projectName, 0755)
	if err != nil {
		fmt.Printf("❌ Error creating directory: %v\n", err)
		os.Exit(1)
	}

	// Write app.hx
	appHxContent := `config {
    database: jsonl
}

slice Tareas {
    field desc: string
    field completada: bool
    
    action Crear() {
        print("Tarea creada: " + desc)
    }

    action Eliminar() {
        print("Tarea eliminada: " + desc)
    }
}

view Home {
    title "Gestor de Tareas Vortex"
    input desc: string
    button "Agregar Tarea" -> Tareas.Crear
    table Tareas -> desc, completada
}

api Router {
    secure route "/tareas" POST -> Tareas.Crear
    secure route "/tareas" DELETE -> Tareas.Eliminar
}
`
	err = os.WriteFile(filepath.Join(projectName, "app.hx"), []byte(appHxContent), 0644)
	if err != nil {
		fmt.Printf("❌ Error writing app.hx: %v\n", err)
		os.Exit(1)
	}

	// Write Makefile
	makefileContent := `.PHONY: dev build run

dev:
	hf dev .

build:
	hf compile . -o server

run:
	hf run .
`
	err = os.WriteFile(filepath.Join(projectName, "Makefile"), []byte(makefileContent), 0644)
	if err != nil {
		fmt.Printf("❌ Error writing Makefile: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("✨ Project '%s' initialized successfully!\n", projectName)
	fmt.Printf("📁 Directory: %s\n", projectName)
	fmt.Printf("👉 Run 'cd %s' and 'hf dev .' to start developing!\n", projectName)
}

// Tracks directory/file modification state
type FileState struct {
	ModTime time.Time
	Size    int64
}

func getSourceState(path string) (map[string]FileState, error) {
	state := make(map[string]FileState)
	info, err := os.Stat(path)
	if err != nil {
		return nil, err
	}

	if !info.IsDir() {
		state[path] = FileState{ModTime: info.ModTime(), Size: info.Size()}
		return state, nil
	}

	err = filepath.WalkDir(path, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !d.IsDir() && filepath.Ext(p) == ".hx" {
			i, err := d.Info()
			if err == nil {
				state[p] = FileState{ModTime: i.ModTime(), Size: i.Size()}
			}
		}
		return nil
	})

	return state, err
}

func hasStateChanged(oldState, newState map[string]FileState) bool {
	if len(oldState) != len(newState) {
		return true
	}
	for k, oldVal := range oldState {
		newVal, exists := newState[k]
		if !exists || oldVal.ModTime.UnixNano() != newVal.ModTime.UnixNano() || oldVal.Size != newVal.Size {
			return true
		}
	}
	return false
}

// Dev watcher loop (Hot Reload)
func handleDev(inputPath string) {
	fmt.Printf("⚡ [Hexagen Dev] Starting live development server on: %s\n", inputPath)

	outputExe := "./temp_dev_server"
	tempCppFile := "temp_codegen.cpp"

	var cmd *exec.Cmd

	stopServer := func() {
		if cmd != nil && cmd.Process != nil {
			fmt.Println("[Hexagen Dev] Stopping running server...")
			// Send SIGKILL to ensure it dies and releases port 8080 immediately
			cmd.Process.Signal(syscall.SIGKILL)
			cmd.Wait()
			cmd = nil
		}
	}

	startServer := func() bool {
		// 1. Transpile using C++ core
		fmt.Println("[Hexagen Dev] Transpiling...")
		cppCode, err := runCore("transpile", inputPath)
		if err != nil {
			fmt.Printf("❌ Transpile failed:\n%v\n", err)
			return false
		}

		err = os.WriteFile(tempCppFile, []byte(cppCode), 0644)
		if err != nil {
			fmt.Printf("❌ Error writing temporary C++ file: %v\n", err)
			return false
		}
		defer os.Remove(tempCppFile)

		// 2. Compile generated C++
		fmt.Println("[Hexagen Dev] Compiling...")
		compileArgs := []string{"-std=c++20", tempCppFile, "-o", outputExe, "-pthread"}
		compileArgs = addModuleFlags(compileArgs)
		compileArgs = addHttpFlags(compileArgs, cppCode)
		compileArgs = addRequiredLibFlags(compileArgs, cppCode)
		if strings.Contains(cppCode, "Database Engine: sqlite") {
			compileArgs = append(compileArgs, "-lsqlite3")
		} else if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
			compileArgs = append(compileArgs, "-lpq")
		} else if strings.Contains(cppCode, "Database Engine: mysql") {
			compileArgs = append(compileArgs, "-lmysqlclient")
		}
		if strings.Contains(cppCode, "#include \"webview.h\"") {
			compileArgs = append(compileArgs, "-DWEBVIEW_GTK")
			compileArgs = append(compileArgs, getPkgConfigFlags()...)
		}
		compileCmd := exec.Command("g++", compileArgs...)
		compileCmd.Stdout = os.Stdout
		compileCmd.Stderr = os.Stderr
		err = compileCmd.Run()
		if err != nil {
			fmt.Println("❌ Compilation failed. Fix syntax errors or missing dependencies to reload.")
			if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
				fmt.Println("\n💡 Tip: It looks like you are using PostgreSQL. Please ensure PostgreSQL development headers are installed:")
				fmt.Println("   - Ubuntu/Debian: sudo apt-get install libpq-dev")
				fmt.Println("   - Fedora/RHEL:   sudo dnf install postgresql-devel")
				fmt.Println("   - macOS:         brew install postgresql")
			} else if strings.Contains(cppCode, "Database Engine: mysql") {
				fmt.Println("\n💡 Tip: It looks like you are using MySQL. Please ensure MySQL development headers are installed:")
				fmt.Println("   - Ubuntu/Debian: sudo apt-get install default-libmysqlclient-dev")
				fmt.Println("   - Fedora/RHEL:   sudo dnf install mysql-devel")
				fmt.Println("   - macOS:         brew install mysql-client")
			} else if strings.Contains(cppCode, "Database Engine: sqlite") {
				fmt.Println("\n💡 Tip: It looks like you are using SQLite. Please ensure SQLite3 development headers are installed:")
				fmt.Println("   - Ubuntu/Debian: sudo apt-get install libsqlite3-dev")
				fmt.Println("   - Fedora/RHEL:   sudo dnf install sqlite-devel")
				fmt.Println("   - macOS:         brew install sqlite")
			}
			return false
		}

		// 3. Launch server
		fmt.Println("[Hexagen Dev] Spawning server in background...")
		cmd = exec.Command(outputExe)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		err = cmd.Start()
		if err != nil {
			fmt.Printf("❌ Error starting server: %v\n", err)
			return false
		}

		fmt.Printf("🚀 [Hexagen Dev] Server running at http://localhost:8080 (PID: %d)\n\n", cmd.Process.Pid)
		return true
	}

	// Clean up on exit (SIGINT/SIGTERM/Ctrl+C)
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		fmt.Println("\n🧹 Cleaning up development processes...")
		stopServer()
		os.Remove(outputExe)
		os.Exit(0)
	}()

	// First compile & run
	startServer()

	// Watcher loop
	lastState, _ := getSourceState(inputPath)
	for {
		time.Sleep(500 * time.Millisecond)
		currentState, err := getSourceState(inputPath)
		if err != nil {
			continue
		}

		if hasStateChanged(lastState, currentState) {
			lastState = currentState
			fmt.Printf("\n⚡ [Hexagen Dev] Change detected in %s! Reloading...\n", inputPath)
			stopServer()
			startServer()
		}
	}
}

func main() {
	if len(os.Args) < 2 {
		printUsage()
		os.Exit(1)
	}

	command := os.Args[1]

	if command == "help" || command == "--help" || command == "-h" {
		printUsage()
		os.Exit(0)
	}

	if command == "new" {
		if len(os.Args) < 3 {
			fmt.Println("Error: Missing project name.")
			fmt.Println("Usage: hf new <project_name>")
			os.Exit(1)
		}
		handleNew(os.Args[2])
		os.Exit(0)
	}

	if command == "dockerize" {
		inputPath := "."
		if len(os.Args) >= 3 {
			inputPath = os.Args[2]
		}
		handleDockerize(inputPath)
		os.Exit(0)
	}

	if command == "db" {
		if len(os.Args) < 3 {
			fmt.Println("Error: Missing db subcommand. Options: diff, migrate, rollback")
			os.Exit(1)
		}
		dbSub := os.Args[2]
		inputPath := "."
		if len(os.Args) >= 4 {
			inputPath = os.Args[3]
		}
		
		switch dbSub {
		case "diff":
			handleDbDiff(inputPath)
		case "migrate":
			handleDbMigrate(inputPath)
		case "rollback":
			handleDbRollback(inputPath)
		default:
			fmt.Printf("Unknown db subcommand: %s. Options: diff, migrate, rollback\n", dbSub)
			os.Exit(1)
		}
		os.Exit(0)
	}

	if command == "add" {
		if len(os.Args) < 3 {
			fmt.Println("Error: Missing package name.")
			fmt.Println("Usage: hf add <package_name>")
			os.Exit(1)
		}
		handleAdd(os.Args[2])
		os.Exit(0)
	}

	// Determine input path and output exe dynamically
	inputPath := "."
	outputExe := "a.out"

	if command == "compile" {
		// Parse compile flags: hf compile [path] -o [output]
		var remainingArgs []string
		for i := 2; i < len(os.Args); i++ {
			if os.Args[i] == "-o" {
				if i+1 < len(os.Args) {
					outputExe = os.Args[i+1]
					i++ // skip output value
				}
			} else {
				remainingArgs = append(remainingArgs, os.Args[i])
			}
		}
		if len(remainingArgs) > 0 {
			inputPath = remainingArgs[0]
		}
	} else {
		// Defaults to "." if path is omitted
		if len(os.Args) >= 3 {
			inputPath = os.Args[2]
		}
		if command == "run" || command == "start" {
			outputExe = "./temp_hexagen_run"
		}
	}

	switch command {
	case "ast":
		out, err := runCore("ast", inputPath)
		if err != nil {
			fmt.Println(err)
			os.Exit(1)
		}
		fmt.Print(out)

	case "transpile":
		out, err := runCore("transpile", inputPath)
		if err != nil {
			fmt.Println(err)
			os.Exit(1)
		}
		fmt.Print(out)

	case "dev":
		handleDev(inputPath)

	case "compile", "run", "start":
		// Transpile
		cppCode, err := runCore("transpile", inputPath)
		if err != nil {
			fmt.Println(err)
			os.Exit(1)
		}

		tempCppFile := "temp_codegen.cpp"
		err = os.WriteFile(tempCppFile, []byte(cppCode), 0644)
		if err != nil {
			fmt.Printf("Error writing temporary C++ file: %v\n", err)
			os.Exit(1)
		}
		defer os.Remove(tempCppFile)

		// Compile
		fmt.Printf("[Hexagen] Compiling generated C++ code to: %s\n", outputExe)
		compileArgs := []string{"-std=c++20", tempCppFile, "-o", outputExe, "-pthread"}
		compileArgs = addModuleFlags(compileArgs)
		compileArgs = addHttpFlags(compileArgs, cppCode)
		compileArgs = addRequiredLibFlags(compileArgs, cppCode)
		if strings.Contains(cppCode, "Database Engine: sqlite") {
			compileArgs = append(compileArgs, "-lsqlite3")
		} else if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
			compileArgs = append(compileArgs, "-lpq")
		} else if strings.Contains(cppCode, "Database Engine: mysql") {
			compileArgs = append(compileArgs, "-lmysqlclient")
		}
		if strings.Contains(cppCode, "#include \"webview.h\"") {
			compileArgs = append(compileArgs, "-DWEBVIEW_GTK")
			compileArgs = append(compileArgs, getPkgConfigFlags()...)
		}
		compileCmd := exec.Command("g++", compileArgs...)
		compileCmd.Stdout = os.Stdout
		compileCmd.Stderr = os.Stderr
		err = compileCmd.Run()
		if err != nil {
			fmt.Println("❌ Error: C++ compilation failed.")
			if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
				fmt.Println("\n💡 Tip: It looks like you are using PostgreSQL. Please ensure PostgreSQL development headers are installed:")
				fmt.Println("   - Ubuntu/Debian: sudo apt-get install libpq-dev")
				fmt.Println("   - Fedora/RHEL:   sudo dnf install postgresql-devel")
				fmt.Println("   - macOS:         brew install postgresql")
			} else if strings.Contains(cppCode, "Database Engine: mysql") {
				fmt.Println("\n💡 Tip: It looks like you are using MySQL. Please ensure MySQL development headers are installed:")
				fmt.Println("   - Ubuntu/Debian: sudo apt-get install default-libmysqlclient-dev")
				fmt.Println("   - Fedora/RHEL:   sudo dnf install mysql-devel")
				fmt.Println("   - macOS:         brew install mysql-client")
			} else if strings.Contains(cppCode, "Database Engine: sqlite") {
				fmt.Println("\n💡 Tip: It looks like you are using SQLite. Please ensure SQLite3 development headers are installed:")
				fmt.Println("   - Ubuntu/Debian: sudo apt-get install libsqlite3-dev")
				fmt.Println("   - Fedora/RHEL:   sudo dnf install sqlite-devel")
				fmt.Println("   - macOS:         brew install sqlite")
			}
			os.Exit(1)
		}

		fmt.Println("[Hexagen] Compilation successful!")

		if command == "run" || command == "start" {
			fmt.Println("[Hexagen] Running executable...\n")
			runCmd := exec.Command(outputExe)
			runCmd.Stdout = os.Stdout
			runCmd.Stderr = os.Stderr
			defer os.Remove(outputExe)
			
			// Handle signals for run command
			sigChan := make(chan os.Signal, 1)
			signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
			go func() {
				<-sigChan
				os.Remove(outputExe)
				os.Exit(0)
			}()

			runCmd.Run()
		}

	case "lsp":
		handleLsp()

	default:
		fmt.Printf("Unknown command: %s\n", command)
		printUsage()
		os.Exit(1)
	}
}

func handleDockerize(inputPath string) {
	fmt.Printf("🐳 [Hexagen Docker] Generating container configurations for: %s\n", inputPath)

	// 1. Transpile code to detect the database engine configuration
	cppCode, err := runCore("transpile", inputPath)
	if err != nil {
		fmt.Printf("❌ Error: Could not transpile project to detect config: %v\n", err)
		os.Exit(1)
	}

	// 2. Detect DB Type
	dbType := "jsonl" // default
	if strings.Contains(cppCode, "Database Engine: sqlite") {
		dbType = "sqlite"
	} else if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
		dbType = "postgres"
	} else if strings.Contains(cppCode, "Database Engine: mysql") {
		dbType = "mysql"
	}

	fmt.Printf("ℹ️  [Hexagen Docker] Detected database engine: %s\n", dbType)

	// 3. Generate Dockerfile
	buildDeps := ""
	runDeps := ""
	switch dbType {
	case "sqlite":
		buildDeps = "libsqlite3-dev"
		runDeps = "libsqlite3-0"
	case "postgres":
		buildDeps = "libpq-dev"
		runDeps = "libpq5"
	case "mysql":
		buildDeps = "default-libmysqlclient-dev"
		runDeps = "libmariadb3"
	}

	dockerfileContent := fmt.Sprintf(`# Dockerfile autogenerado por Hexagen Framework
# --- Etapa de Compilación ---
FROM gcc:13 AS builder

# Instalar dependencias necesarias para compilar el núcleo
RUN apt-get update && apt-get install -y \
    curl \
    %s \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Descargar las herramientas de compilación de Hexagen (versión %s)
RUN curl -L https://github.com/rbaezc/hexagenframework/releases/download/%s/hf_linux_x86_64 -o hf && chmod +x hf
RUN curl -L https://github.com/rbaezc/hexagenframework/releases/download/%s/hf_core_linux_x86_64 -o hf_core && chmod +x hf_core

# Copiar el código fuente del proyecto
COPY . .

# Asegurar que existan directorios y archivos para evitar fallos de copia
RUN mkdir -p public
RUN touch .env

# Transpilar y compilar la aplicación a producción
RUN ./hf compile . -o server_release

# --- Etapa de Ejecución ---
FROM debian:stable-slim

# Instalar dependencias de ejecución de la base de datos y certificados CA
RUN apt-get update && apt-get install -y \
    ca-certificates \
    %s \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /data

# Copiar el servidor compilado, directorio público y archivo .env si existiesen
COPY --from=builder /app/server_release /usr/local/bin/server_release
COPY --from=builder /app/public /data/public
COPY --from=builder /app/.env /data/.env

EXPOSE 8080

CMD ["/usr/local/bin/server_release"]
`, buildDeps, Version, Version, Version, runDeps)

	// Write Dockerfile
	dockerfilePath := filepath.Join(inputPath, "Dockerfile")
	err = os.WriteFile(dockerfilePath, []byte(dockerfileContent), 0644)
	if err != nil {
		fmt.Printf("❌ Error al escribir Dockerfile: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("✅ [Hexagen Docker] 'Dockerfile' generado con éxito.")

	// 4. Generate docker-compose.yml
	var composeContent string
	switch dbType {
	case "postgres":
		composeContent = `version: '3.8'

services:
  web:
    build:
      context: .
      dockerfile: Dockerfile
    ports:
      - "8080:8080"
    environment:
      - DB_HOST=db
      - DB_PORT=5432
      - DB_USER=hexagen_user
      - DB_PASS=hexagen_password
      - DB_NAME=hexagen_db
      - JWT_SECRET=cambiar_en_produccion
    depends_on:
      - db
    volumes:
      - hexagen-data:/data

  db:
    image: postgres:15-alpine
    environment:
      - POSTGRES_USER=hexagen_user
      - POSTGRES_PASSWORD=hexagen_password
      - POSTGRES_DB=hexagen_db
    ports:
      - "5432:5432"
    volumes:
      - postgres-data:/var/lib/postgresql/data

volumes:
  postgres-data:
  hexagen-data:
`
	case "mysql":
		composeContent = `version: '3.8'

services:
  web:
    build:
      context: .
      dockerfile: Dockerfile
    ports:
      - "8080:8080"
    environment:
      - DB_HOST=db
      - DB_PORT=3306
      - DB_USER=hexagen_user
      - DB_PASS=hexagen_password
      - DB_NAME=hexagen_db
      - JWT_SECRET=cambiar_en_produccion
    depends_on:
      - db
    volumes:
      - hexagen-data:/data

  db:
    image: mysql:8.0
    environment:
      - MYSQL_ROOT_PASSWORD=hexagen_root_password
      - MYSQL_DATABASE=hexagen_db
      - MYSQL_USER=hexagen_user
      - MYSQL_PASSWORD=hexagen_password
    ports:
      - "3306:3306"
    volumes:
      - mysql-data:/var/lib/mysql

volumes:
  mysql-data:
  hexagen-data:
`
	case "sqlite", "jsonl":
		composeContent = `version: '3.8'

services:
  web:
    build:
      context: .
      dockerfile: Dockerfile
    ports:
      - "8080:8080"
    environment:
      - JWT_SECRET=cambiar_en_produccion
    volumes:
      - hexagen-data:/data

volumes:
  hexagen-data:
`
	}

	// Write docker-compose.yml
	composePath := filepath.Join(inputPath, "docker-compose.yml")
	err = os.WriteFile(composePath, []byte(composeContent), 0644)
	if err != nil {
		fmt.Printf("❌ Error al escribir docker-compose.yml: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("✅ [Hexagen Docker] 'docker-compose.yml' generado con éxito.")
	fmt.Println("\n🚀 ¡Configuración lista! Puedes iniciar tu entorno de producción ejecutando:")
	fmt.Println("   docker-compose up --build -d")
}

func handleDbDiff(inputPath string) {
	fmt.Printf("🔍 [Hexagen DB] Generating incremental schema migration for: %s\n", inputPath)

	// 1. Get database type from transpiled code
	cppCode, err := runCore("transpile", inputPath)
	if err != nil {
		fmt.Printf("❌ Error: Could not transpile project to detect config: %v\n", err)
		os.Exit(1)
	}

	dbType := "jsonl"
	if strings.Contains(cppCode, "Database Engine: sqlite") {
		dbType = "sqlite"
	} else if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
		dbType = "postgres"
	} else if strings.Contains(cppCode, "Database Engine: mysql") {
		dbType = "mysql"
	}

	fmt.Printf("ℹ️  [Hexagen DB] Database engine detected: %s\n", dbType)

	// 2. Fetch current schema representation in JSON from C++ core
	schemaJson, err := runCore("schema", inputPath)
	if err != nil {
		fmt.Printf("❌ Error: Could not parse schema: %v\n", err)
		os.Exit(1)
	}

	var currentSchema map[string]map[string]string
	err = json.Unmarshal([]byte(schemaJson), &currentSchema)
	if err != nil {
		fmt.Printf("❌ Error: Could not parse schema JSON: %v\n", err)
		os.Exit(1)
	}

	// 3. Load old schema from db/schema.json
	oldSchema := make(map[string]map[string]string)
	schemaJsonPath := filepath.Join(inputPath, "db", "schema.json")
	if _, err := os.Stat(schemaJsonPath); err == nil {
		oldSchemaBytes, err := os.ReadFile(schemaJsonPath)
		if err == nil {
			json.Unmarshal(oldSchemaBytes, &oldSchema)
		}
	}

	// 4. Compute schema difference
	upSqls := []string{}
	downSqls := []string{}

	// Helper functions for SQL generation
	quoteName := func(name string, db string) string {
		if db == "mysql" {
			return "`" + name + "`"
		}
		return "\"" + name + "\""
	}

	getSqlType := func(fieldStr string, db string) string {
		if fieldStr == "int" {
			if db == "sqlite" {
				return "INTEGER"
			}
			return "INT"
		}
		if strings.HasPrefix(fieldStr, "relation(") {
			relatedSlice := strings.TrimSuffix(strings.TrimPrefix(fieldStr, "relation("), ")")
			refTable := quoteName(relatedSlice, db)
			if db == "sqlite" {
				return "INTEGER REFERENCES " + refTable + "(id)"
			}
			return "INT REFERENCES " + refTable + "(id)"
		}
		if fieldStr == "float" {
			if db == "mysql" {
				return "DOUBLE"
			}
			return "REAL"
		}
		if fieldStr == "bool" {
			if db == "sqlite" {
				return "INTEGER"
			}
			if db == "postgres" {
				return "BOOLEAN"
			}
			return "TINYINT(1)"
		}
		if db == "sqlite" {
			return "TEXT"
		}
		return "VARCHAR(255)"
	}

	// Compare tables
	for sliceName, fields := range currentSchema {
		if _, exists := oldSchema[sliceName]; !exists {
			// CREATE TABLE
			qTable := quoteName(sliceName, dbType)
			
			var idCol string
			if dbType == "sqlite" {
				idCol = "id INTEGER PRIMARY KEY AUTOINCREMENT"
			} else if dbType == "postgres" {
				idCol = "id SERIAL PRIMARY KEY"
			} else {
				idCol = "id INT AUTO_INCREMENT PRIMARY KEY"
			}
			
			cols := []string{idCol}
			for fName, fType := range fields {
				cols = append(cols, fmt.Sprintf("%s %s", quoteName(fName, dbType), getSqlType(fType, dbType)))
			}
			
			upSqls = append(upSqls, fmt.Sprintf("CREATE TABLE %s (%s);", qTable, strings.Join(cols, ", ")))
			downSqls = append(downSqls, fmt.Sprintf("DROP TABLE %s;", qTable))
		} else {
			// Check added/modified columns
			oldFields := oldSchema[sliceName]
			qTable := quoteName(sliceName, dbType)
			
			for fName, fType := range fields {
				if oldType, fExists := oldFields[fName]; !fExists {
					// ADD COLUMN
					upSqls = append(upSqls, fmt.Sprintf("ALTER TABLE %s ADD COLUMN %s %s;", qTable, quoteName(fName, dbType), getSqlType(fType, dbType)))
					downSqls = append(downSqls, fmt.Sprintf("ALTER TABLE %s DROP COLUMN %s;", qTable, quoteName(fName, dbType)))
				} else if oldType != fType {
					// MODIFY COLUMN
					if dbType == "postgres" {
						upSqls = append(upSqls, fmt.Sprintf("ALTER TABLE %s ALTER COLUMN %s TYPE %s;", qTable, quoteName(fName, dbType), getSqlType(fType, dbType)))
						downSqls = append(downSqls, fmt.Sprintf("ALTER TABLE %s ALTER COLUMN %s TYPE %s;", qTable, quoteName(fName, dbType), getSqlType(oldType, dbType)))
					} else if dbType == "mysql" {
						upSqls = append(upSqls, fmt.Sprintf("ALTER TABLE %s MODIFY COLUMN %s %s;", qTable, quoteName(fName, dbType), getSqlType(fType, dbType)))
						downSqls = append(downSqls, fmt.Sprintf("ALTER TABLE %s MODIFY COLUMN %s %s;", qTable, quoteName(fName, dbType), getSqlType(oldType, dbType)))
					}
				}
			}
			
			// Check removed columns
			for fName, oldType := range oldFields {
				if _, fExists := fields[fName]; !fExists {
					// DROP COLUMN
					upSqls = append(upSqls, fmt.Sprintf("ALTER TABLE %s DROP COLUMN %s;", qTable, quoteName(fName, dbType)))
					downSqls = append(downSqls, fmt.Sprintf("ALTER TABLE %s ADD COLUMN %s %s;", qTable, quoteName(fName, dbType), getSqlType(oldType, dbType)))
				}
			}
		}
	}

	// Check removed tables
	for sliceName, fields := range oldSchema {
		if _, exists := currentSchema[sliceName]; !exists {
			// DROP TABLE
			qTable := quoteName(sliceName, dbType)
			upSqls = append(upSqls, fmt.Sprintf("DROP TABLE %s;", qTable))
			
			var idCol string
			if dbType == "sqlite" {
				idCol = "id INTEGER PRIMARY KEY AUTOINCREMENT"
			} else if dbType == "postgres" {
				idCol = "id SERIAL PRIMARY KEY"
			} else {
				idCol = "id INT AUTO_INCREMENT PRIMARY KEY"
			}
			
			cols := []string{idCol}
			for fName, fType := range fields {
				cols = append(cols, fmt.Sprintf("%s %s", quoteName(fName, dbType), getSqlType(fType, dbType)))
			}
			downSqls = append(downSqls, fmt.Sprintf("CREATE TABLE %s (%s);", qTable, strings.Join(cols, ", ")))
		}
	}

	if len(upSqls) == 0 {
		fmt.Println("ℹ️  [Hexagen DB] No schema changes detected. Migrations are up to date.")
		return
	}

	// 5. Write SQL migrations files
	migrationsDir := filepath.Join(inputPath, "db", "migrations")
	err = os.MkdirAll(migrationsDir, 0755)
	if err != nil {
		fmt.Printf("❌ Error: Could not create migrations directory: %v\n", err)
		os.Exit(1)
	}

	timestamp := time.Now().Format("20060102150405")
	
	upPath := filepath.Join(migrationsDir, timestamp+"_migration.up.sql")
	err = os.WriteFile(upPath, []byte(strings.Join(upSqls, "\n")+"\n"), 0644)
	if err != nil {
		fmt.Printf("❌ Error writing up migration file: %v\n", err)
		os.Exit(1)
	}

	downPath := filepath.Join(migrationsDir, timestamp+"_migration.down.sql")
	err = os.WriteFile(downPath, []byte(strings.Join(downSqls, "\n")+"\n"), 0644)
	if err != nil {
		fmt.Printf("❌ Error writing down migration file: %v\n", err)
		os.Exit(1)
	}

	// 6. Write schema snapshot
	err = os.MkdirAll(filepath.Join(inputPath, "db"), 0755)
	if err == nil {
		schemaBytes, _ := json.MarshalIndent(currentSchema, "", "  ")
		os.WriteFile(schemaJsonPath, schemaBytes, 0644)
	}

	fmt.Printf("✅ [Hexagen DB] Migration files generated in db/migrations/:\n  - %s_migration.up.sql\n  - %s_migration.down.sql\n", timestamp, timestamp)
}

func handleDbMigrate(inputPath string) {
	fmt.Printf("🚀 [Hexagen DB] Running pending migrations for: %s\n", inputPath)
	runMigrationTool(inputPath, "migrate")
}

func handleDbRollback(inputPath string) {
	fmt.Printf("🔄 [Hexagen DB] Rolling back last migration for: %s\n", inputPath)
	runMigrationTool(inputPath, "rollback")
}

func getMigrationFiles(dir string, suffix string) ([]string, error) {
	var files []string
	entries, err := os.ReadDir(dir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	for _, entry := range entries {
		if !entry.IsDir() && strings.HasSuffix(entry.Name(), suffix) {
			files = append(files, filepath.Join(dir, entry.Name()))
		}
	}
	return files, nil
}

func splitStatements(sql string) []string {
	var stmts []string
	raw := strings.Split(sql, ";")
	for _, s := range raw {
		trimmed := strings.TrimSpace(s)
		if trimmed != "" {
			stmts = append(stmts, trimmed)
		}
	}
	return stmts
}

func escapeCppString(s string) string {
	s = strings.ReplaceAll(s, "\\", "\\\\")
	s = strings.ReplaceAll(s, "\"", "\\\"")
	s = strings.ReplaceAll(s, "\n", "\\n")
	s = strings.ReplaceAll(s, "\r", "")
	return s
}

func getDbTypeStruct(dbType string) string {
	if dbType == "sqlite" {
		return "sqlite3"
	}
	if dbType == "postgres" {
		return "PGconn"
	}
	return "MYSQL"
}

func getDbTypeVar(dbType string) string {
	if dbType == "sqlite" {
		return "db"
	}
	return "conn"
}

func getDbTypeGetter(dbType string) string {
	if dbType == "sqlite" {
		return "getSQLiteConn"
	}
	if dbType == "postgres" {
		return "getPGConn"
	}
	return "getMySQLConn"
}

func runMigrationTool(inputPath string, mode string) {
	// 1. Transpile to detect the DB type
	cppCode, err := runCore("transpile", inputPath)
	if err != nil {
		fmt.Printf("❌ Error: Could not transpile project: %v\n", err)
		os.Exit(1)
	}

	dbType := "jsonl"
	if strings.Contains(cppCode, "Database Engine: sqlite") {
		dbType = "sqlite"
	} else if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
		dbType = "postgres"
	} else if strings.Contains(cppCode, "Database Engine: mysql") {
		dbType = "mysql"
	}

	if dbType == "jsonl" {
		fmt.Println("ℹ️  [Hexagen DB] Database engine is JSONL. Schema is managed dynamically from schema.json. No migrations need to be executed on a database.")
		return
	}

	// 2. Read migration files
	migrationsDir := filepath.Join(inputPath, "db", "migrations")
	upFiles, err := getMigrationFiles(migrationsDir, ".up.sql")
	if err != nil {
		fmt.Printf("❌ Error reading up migrations: %v\n", err)
		os.Exit(1)
	}
	downFiles, err := getMigrationFiles(migrationsDir, ".down.sql")
	if err != nil {
		fmt.Printf("❌ Error reading down migrations: %v\n", err)
		os.Exit(1)
	}

	// 3. Generate static migration vectors in C++
	var upVectorCpp strings.Builder
	for _, f := range upFiles {
		version := strings.Split(filepath.Base(f), "_")[0]
		bytes, err := os.ReadFile(f)
		if err != nil {
			fmt.Printf("❌ Error reading migration file %s: %v\n", f, err)
			os.Exit(1)
		}
		stmts := splitStatements(string(bytes))
		
		upVectorCpp.WriteString("    {\n")
		upVectorCpp.WriteString(fmt.Sprintf("        Migration m;\n        m.version = \"%s\";\n", version))
		for _, stmt := range stmts {
			upVectorCpp.WriteString(fmt.Sprintf("        m.statements.push_back(\"%s\");\n", escapeCppString(stmt)))
		}
		upVectorCpp.WriteString("        migrations.push_back(m);\n    }\n")
	}

	var downVectorCpp strings.Builder
	for _, f := range downFiles {
		version := strings.Split(filepath.Base(f), "_")[0]
		bytes, err := os.ReadFile(f)
		if err != nil {
			fmt.Printf("❌ Error reading migration file %s: %v\n", f, err)
			os.Exit(1)
		}
		stmts := splitStatements(string(bytes))
		
		downVectorCpp.WriteString("    {\n")
		downVectorCpp.WriteString(fmt.Sprintf("        Migration m;\n        m.version = \"%s\";\n", version))
		for _, stmt := range stmts {
			downVectorCpp.WriteString(fmt.Sprintf("        m.statements.push_back(\"%s\");\n", escapeCppString(stmt)))
		}
		downVectorCpp.WriteString("        rollbacks.push_back(m);\n    }\n")
	}

	// 4. Generate C++ runner templates
	var headers string
	var connHelpers string
	var executionBlocks string

	if dbType == "sqlite" {
		headers = `#include <sqlite3.h>`
		connHelpers = `sqlite3* getSQLiteConn() {
    std::string dbName = getEnvOr("DB_NAME", "vortex_db.db");
    sqlite3* db = nullptr;
    int rc = sqlite3_open(dbName.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "[SQLite] Can't open database: " << sqlite3_errmsg(db) << std::endl;
        if (db) sqlite3_close(db);
        return nullptr;
    }
    return db;
}`
		executionBlocks = `
    if (mode == "migrate") {
        std::string qInit = "CREATE TABLE IF NOT EXISTS _hexagen_schema_migrations (version TEXT PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);";
        char* errMsg = nullptr;
        sqlite3_exec(db, qInit.c_str(), nullptr, nullptr, &errMsg);

        std::set<std::string> applied;
        std::string qFetch = "SELECT version FROM _hexagen_schema_migrations;";
        auto callback = [](void* data, int argc, char** argv, char** colName) -> int {
            auto* setPtr = static_cast<std::set<std::string>*>(data);
            if (argc > 0 && argv[0]) {
                setPtr->insert(argv[0]);
            }
            return 0;
        };
        sqlite3_exec(db, qFetch.c_str(), callback, &applied, &errMsg);

        bool appliedAny = false;
        for (const auto& m : migrations) {
            if (applied.find(m.version) == applied.end()) {
                std::cout << "[Hexagen DB] Applying migration: " << m.version << "..." << std::endl;
                for (const auto& stmt : m.statements) {
                    int rc = sqlite3_exec(db, stmt.c_str(), nullptr, nullptr, &errMsg);
                    if (rc != SQLITE_OK) {
                        std::cerr << "❌ [SQLite Error] Failed to execute statement: " << stmt << "\nReason: " << errMsg << std::endl;
                        sqlite3_free(errMsg);
                        sqlite3_close(db);
                        return 1;
                    }
                }
                std::string qInsert = "INSERT INTO _hexagen_schema_migrations (version) VALUES ('" + m.version + "');";
                sqlite3_exec(db, qInsert.c_str(), nullptr, nullptr, &errMsg);
                std::cout << "✅ [Hexagen DB] Migration " << m.version << " applied successfully!" << std::endl;
                appliedAny = true;
            }
        }
        if (!appliedAny) {
            std::cout << "ℹ️  [Hexagen DB] No pending migrations to apply." << std::endl;
        }
        sqlite3_close(db);
        return 0;
    } else if (mode == "rollback") {
        std::string qInit = "CREATE TABLE IF NOT EXISTS _hexagen_schema_migrations (version TEXT PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);";
        char* errMsg = nullptr;
        sqlite3_exec(db, qInit.c_str(), nullptr, nullptr, &errMsg);

        std::string lastAppliedVersion = "";
        std::string qFetch = "SELECT version FROM _hexagen_schema_migrations ORDER BY applied_at DESC, version DESC LIMIT 1;";
        auto callback = [](void* data, int argc, char** argv, char** colName) -> int {
            auto* strPtr = static_cast<std::string*>(data);
            if (argc > 0 && argv[0]) {
                *strPtr = argv[0];
            }
            return 0;
        };
        sqlite3_exec(db, qFetch.c_str(), callback, &lastAppliedVersion, &errMsg);

        if (lastAppliedVersion.empty()) {
            std::cout << "ℹ️  [Hexagen DB] No applied migrations found to rollback." << std::endl;
            sqlite3_close(db);
            return 0;
        }

        bool found = false;
        for (const auto& r : rollbacks) {
            if (r.version == lastAppliedVersion) {
                std::cout << "[Hexagen DB] Rolling back migration: " << r.version << "..." << std::endl;
                for (const auto& stmt : r.statements) {
                    int rc = sqlite3_exec(db, stmt.c_str(), nullptr, nullptr, &errMsg);
                    if (rc != SQLITE_OK) {
                        std::cerr << "❌ [SQLite Error] Failed to execute statement during rollback: " << stmt << "\nReason: " << errMsg << std::endl;
                        sqlite3_free(errMsg);
                        sqlite3_close(db);
                        return 1;
                    }
                }
                std::string qDelete = "DELETE FROM _hexagen_schema_migrations WHERE version = '" + r.version + "';";
                sqlite3_exec(db, qDelete.c_str(), nullptr, nullptr, &errMsg);
                std::cout << "✅ [Hexagen DB] Rollback of migration " << r.version << " completed successfully!" << std::endl;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "❌ [Hexagen DB] Error: Rollback instructions not found for version " << lastAppliedVersion << std::endl;
            sqlite3_close(db);
            return 1;
        }
        sqlite3_close(db);
        return 0;
    }
`
	} else if (dbType == "postgres") {
		headers = `#include <libpq-fe.h>`
		connHelpers = `PGconn* getPGConn() {
    if (!std::getenv("DB_HOST") && !std::getenv("DB_USER")) return nullptr;
    std::string conninfo = "host=" + std::string(getEnvOr("DB_HOST", "localhost")) +
                           " port=" + std::string(getEnvOr("DB_PORT", "5432")) +
                           " dbname=" + std::string(getEnvOr("DB_NAME", "vortex_db")) +
                           " user=" + std::string(getEnvOr("DB_USER", "postgres")) +
                           " password=" + std::string(getEnvOr("DB_PASS", ""));
    PGconn* conn = PQconnectdb(conninfo.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "[PostgreSQL] Connection failed: " << PQerrorMessage(conn) << std::endl;
        PQfinish(conn);
        return nullptr;
    }
    return conn;
}`
		executionBlocks = `
    if (mode == "migrate") {
        std::string qInit = "CREATE TABLE IF NOT EXISTS _hexagen_schema_migrations (version VARCHAR(255) PRIMARY KEY, applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";
        PGresult* res = PQexec(conn, qInit.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::cerr << "❌ [PostgreSQL Error] Failed to create tracking table: " << PQerrorMessage(conn) << std::endl;
            PQclear(res);
            PQfinish(conn);
            return 1;
        }
        PQclear(res);

        std::set<std::string> applied;
        std::string qFetch = "SELECT version FROM _hexagen_schema_migrations;";
        res = PQexec(conn, qFetch.c_str());
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
            int rows = PQntuples(res);
            for (int i = 0; i < rows; ++i) {
                applied.insert(PQgetvalue(res, i, 0));
            }
        }
        PQclear(res);

        bool appliedAny = false;
        for (const auto& m : migrations) {
            if (applied.find(m.version) == applied.end()) {
                std::cout << "[Hexagen DB] Applying migration: " << m.version << "..." << std::endl;
                for (const auto& stmt : m.statements) {
                    PGresult* stmtRes = PQexec(conn, stmt.c_str());
                    if (PQresultStatus(stmtRes) != PGRES_COMMAND_OK) {
                        std::cerr << "❌ [PostgreSQL Error] Failed to execute statement: " << stmt << "\nReason: " << PQerrorMessage(conn) << std::endl;
                        PQclear(stmtRes);
                        PQfinish(conn);
                        return 1;
                    }
                    PQclear(stmtRes);
                }
                std::string qInsert = "INSERT INTO _hexagen_schema_migrations (version) VALUES ('" + m.version + "');";
                PGresult* insRes = PQexec(conn, qInsert.c_str());
                PQclear(insRes);
                std::cout << "✅ [Hexagen DB] Migration " << m.version << " applied successfully!" << std::endl;
                appliedAny = true;
            }
        }
        if (!appliedAny) {
            std::cout << "ℹ️  [Hexagen DB] No pending migrations to apply." << std::endl;
        }
        PQfinish(conn);
        return 0;
    } else if (mode == "rollback") {
        std::string lastAppliedVersion = "";
        std::string qFetch = "SELECT version FROM _hexagen_schema_migrations ORDER BY applied_at DESC, version DESC LIMIT 1;";
        PGresult* res = PQexec(conn, qFetch.c_str());
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            lastAppliedVersion = PQgetvalue(res, 0, 0);
        }
        PQclear(res);

        if (lastAppliedVersion.empty()) {
            std::cout << "ℹ️  [Hexagen DB] No applied migrations found to rollback." << std::endl;
            PQfinish(conn);
            return 0;
        }

        bool found = false;
        for (const auto& r : rollbacks) {
            if (r.version == lastAppliedVersion) {
                std::cout << "[Hexagen DB] Rolling back migration: " << r.version << "..." << std::endl;
                for (const auto& stmt : r.statements) {
                    PGresult* stmtRes = PQexec(conn, stmt.c_str());
                    if (PQresultStatus(stmtRes) != PGRES_COMMAND_OK) {
                        std::cerr << "❌ [PostgreSQL Error] Failed to execute statement during rollback: " << stmt << "\nReason: " << PQerrorMessage(conn) << std::endl;
                        PQclear(stmtRes);
                        PQfinish(conn);
                        return 1;
                    }
                    PQclear(stmtRes);
                }
                std::string qDelete = "DELETE FROM _hexagen_schema_migrations WHERE version = '" + r.version + "';";
                PGresult* delRes = PQexec(conn, qDelete.c_str());
                PQclear(delRes);
                std::cout << "✅ [Hexagen DB] Rollback of migration " << r.version << " completed successfully!" << std::endl;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "❌ [Hexagen DB] Error: Rollback instructions not found for version " << lastAppliedVersion << std::endl;
            PQfinish(conn);
            return 1;
        }
        PQfinish(conn);
        return 0;
    }
`
	} else if (dbType == "mysql") {
		headers = `#include <mysql/mysql.h>`
		connHelpers = `MYSQL* getMySQLConn() {
    if (!std::getenv("DB_HOST") && !std::getenv("DB_USER")) return nullptr;
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        std::cerr << "[MySQL] Initialization failed" << std::endl;
        return nullptr;
    }
    const char* host = getEnvOr("DB_HOST", "127.0.0.1");
    const char* user = getEnvOr("DB_USER", "root");
    const char* pass = getEnvOr("DB_PASS", "");
    const char* db = getEnvOr("DB_NAME", "vortex_db");
    const char* portStr = getEnvOr("DB_PORT", "3306");
    int port = std::stoi(portStr);
    if (!mysql_real_connect(conn, host, user, pass, db, port, nullptr, 0)) {
        std::cerr << "[MySQL] Connection failed: " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}`
		executionBlocks = `
    if (mode == "migrate") {
        std::string qInit = "CREATE TABLE IF NOT EXISTS _hexagen_schema_migrations (version VARCHAR(255) PRIMARY KEY, applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";
        mysql_query(conn, qInit.c_str());

        std::set<std::string> applied;
        std::string qFetch = "SELECT version FROM _hexagen_schema_migrations;";
        if (mysql_query(conn, qFetch.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    applied.insert(row[0]);
                }
                mysql_free_result(res);
            }
        }

        bool appliedAny = false;
        for (const auto& m : migrations) {
            if (applied.find(m.version) == applied.end()) {
                std::cout << "[Hexagen DB] Applying migration: " << m.version << "..." << std::endl;
                for (const auto& stmt : m.statements) {
                    if (mysql_query(conn, stmt.c_str()) != 0) {
                        std::cerr << "❌ [MySQL Error] Failed to execute statement: " << stmt << "\nReason: " << mysql_error(conn) << std::endl;
                        mysql_close(conn);
                        return 1;
                    }
                }
                std::string qInsert = "INSERT INTO _hexagen_schema_migrations (version) VALUES ('" + m.version + "');";
                mysql_query(conn, qInsert.c_str());
                std::cout << "✅ [Hexagen DB] Migration " << m.version << " applied successfully!" << std::endl;
                appliedAny = true;
            }
        }
        if (!appliedAny) {
            std::cout << "ℹ️  [Hexagen DB] No pending migrations to apply." << std::endl;
        }
        mysql_close(conn);
        return 0;
    } else if (mode == "rollback") {
        std::string lastAppliedVersion = "";
        std::string qFetch = "SELECT version FROM _hexagen_schema_migrations ORDER BY applied_at DESC, version DESC LIMIT 1;";
        if (mysql_query(conn, qFetch.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row) {
                    lastAppliedVersion = row[0];
                }
                mysql_free_result(res);
            }
        }

        if (lastAppliedVersion.empty()) {
            std::cout << "ℹ️  [Hexagen DB] No applied migrations found to rollback." << std::endl;
            mysql_close(conn);
            return 0;
        }

        bool found = false;
        for (const auto& r : rollbacks) {
            if (r.version == lastAppliedVersion) {
                std::cout << "[Hexagen DB] Rolling back migration: " << r.version << "..." << std::endl;
                for (const auto& stmt : r.statements) {
                    if (mysql_query(conn, stmt.c_str()) != 0) {
                        std::cerr << "❌ [MySQL Error] Failed to execute statement during rollback: " << stmt << "\nReason: " << mysql_error(conn) << std::endl;
                        mysql_close(conn);
                        return 1;
                    }
                }
                std::string qDelete = "DELETE FROM _hexagen_schema_migrations WHERE version = '" + r.version + "';";
                mysql_query(conn, qDelete.c_str());
                std::cout << "✅ [Hexagen DB] Rollback of migration " << r.version << " completed successfully!" << std::endl;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "❌ [Hexagen DB] Error: Rollback instructions not found for version " << lastAppliedVersion << std::endl;
            mysql_close(conn);
            return 1;
        }
        mysql_close(conn);
        return 0;
    }
`
	}

	runnerTemplate := fmt.Sprintf(`#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <cstdlib>
%s

std::string getEnvOr(const std::string& key, const std::string& defaultVal) {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : defaultVal;
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void loadEnv() {
    std::ifstream envFile(".env");
    if (!envFile.is_open()) return;
    std::string line;
    while (std::getline(envFile, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        std::string key = trim(line.substr(0, eqPos));
        std::string val = trim(line.substr(eqPos + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }
        setenv(key.c_str(), val.c_str(), 1);
    }
    envFile.close();
}

%s

struct Migration {
    std::string version;
    std::vector<std::string> statements;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [migrate|rollback]" << std::endl;
        return 1;
    }
    std::string mode = argv[1];

    loadEnv();
    
    %s* %s = %s();
    if (!%s) {
        std::cerr << "❌ Error: Could not connect to the database." << std::endl;
        return 1;
    }

    std::vector<Migration> migrations;
    std::vector<Migration> rollbacks;

%s
%s

%s
}
`, headers, connHelpers, 
   getDbTypeStruct(dbType), getDbTypeVar(dbType), getDbTypeGetter(dbType), getDbTypeVar(dbType),
   upVectorCpp.String(), downVectorCpp.String(), executionBlocks)

	// 5. Compile and run C++ runner
	tempCppFile := filepath.Join(inputPath, "temp_migration_runner.cpp")
	tempExe := filepath.Join(inputPath, "temp_migration_runner")

	err = os.WriteFile(tempCppFile, []byte(runnerTemplate), 0644)
	if err != nil {
		fmt.Printf("❌ Error writing temporary C++ runner: %v\n", err)
		os.Exit(1)
	}
	defer os.Remove(tempCppFile)

	compileArgs := []string{"-std=c++17", tempCppFile, "-o", tempExe, "-pthread"}
	if dbType == "sqlite" {
		compileArgs = append(compileArgs, "-lsqlite3")
	} else if dbType == "postgres" {
		compileArgs = append(compileArgs, "-lpq")
	} else if dbType == "mysql" {
		compileArgs = append(compileArgs, "-lmysqlclient")
	}

	compileCmd := exec.Command("g++", compileArgs...)
	compileCmd.Stdout = os.Stdout
	compileCmd.Stderr = os.Stderr
	err = compileCmd.Run()
	if err != nil {
		fmt.Printf("❌ Error compiling migration runner: %v\n", err)
		os.Exit(1)
	}
	defer os.Remove(tempExe)

	// Run migration tool
	absExe, err := filepath.Abs(tempExe)
	if err != nil {
		fmt.Printf("❌ Error resolving path to migration tool: %v\n", err)
		os.Exit(1)
	}
	runCmd := exec.Command(absExe, mode)
	runCmd.Dir = inputPath
	runCmd.Stdout = os.Stdout
	runCmd.Stderr = os.Stderr
	err = runCmd.Run()
	if err != nil {
		fmt.Printf("❌ Error executing migrations: %v\n", err)
		os.Exit(1)
	}
}

// ==========================================
// Hexagen Language Server (LSP) Implementation
// ==========================================

type LspRequest struct {
	JsonRpc string          `json:"jsonrpc"`
	Id      interface{}     `json:"id,omitempty"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params,omitempty"`
}

type LspResponse struct {
	JsonRpc string      `json:"jsonrpc"`
	Id      interface{} `json:"id"`
	Result  interface{} `json:"result,omitempty"`
	Error   interface{} `json:"error,omitempty"`
}

type LspNotification struct {
	JsonRpc string      `json:"jsonrpc"`
	Method  string      `json:"method"`
	Params  interface{} `json:"params"`
}

type InitializeResult struct {
	Capabilities ServerCapabilities `json:"capabilities"`
}

type ServerCapabilities struct {
	TextDocumentSync   int                 `json:"textDocumentSync"`
	CompletionProvider *CompletionOptions  `json:"completionProvider,omitempty"`
}

type CompletionOptions struct {
	TriggerCharacters []string `json:"triggerCharacters,omitempty"`
}

type TextDocumentParams struct {
	TextDocument TextDocumentItem `json:"textDocument"`
}

type TextDocumentItem struct {
	Uri        string `json:"uri"`
	LanguageId string `json:"languageId"`
	Version    int    `json:"version"`
	Text       string `json:"text"`
}

type DidChangeParams struct {
	TextDocument   VersionedTextDocumentIdentifier `json:"textDocument"`
	ContentChanges []TextDocumentContentChangeEvent `json:"contentChanges"`
}

type VersionedTextDocumentIdentifier struct {
	Uri     string `json:"uri"`
	Version int    `json:"version"`
}

type TextDocumentContentChangeEvent struct {
	Text string `json:"text"`
}

type PublishDiagnosticsParams struct {
	Uri         string       `json:"uri"`
	Diagnostics []Diagnostic `json:"diagnostics"`
}

type Diagnostic struct {
	Range    Range  `json:"range"`
	Severity int    `json:"severity"`
	Source   string `json:"source"`
	Message  string `json:"message"`
}

type Range struct {
	Start Position `json:"start"`
	End   Position `json:"end"`
}

type Position struct {
	Line      int `json:"line"`
	Character int `json:"character"`
}

type CompletionParams struct {
	TextDocument TextDocumentIdentifier `json:"textDocument"`
	Position     Position               `json:"position"`
}

type TextDocumentIdentifier struct {
	Uri string `json:"uri"`
}

type CompletionItem struct {
	Label         string `json:"label"`
	Kind          int    `json:"kind"`
	Detail        string `json:"detail,omitempty"`
	Documentation string `json:"documentation,omitempty"`
}

var lspWriteMutex sync.Mutex

func sendLspMessage(msg interface{}) {
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	lspWriteMutex.Lock()
	defer lspWriteMutex.Unlock()
	fmt.Printf("Content-Length: %d\r\n\r\n%s", len(data), string(data))
	os.Stdout.Sync()
}

func sendLspResponse(id interface{}, result interface{}, err interface{}) {
	resp := LspResponse{
		JsonRpc: "2.0",
		Id:      id,
		Result:  result,
		Error:   err,
	}
	sendLspMessage(resp)
}

func sendLspNotification(method string, params interface{}) {
	notif := LspNotification{
		JsonRpc: "2.0",
		Method:  method,
		Params:  params,
	}
	sendLspMessage(notif)
}

func handleLsp() {
	reader := bufio.NewReader(os.Stdin)
	for {
		var contentLength int
		for {
			line, err := reader.ReadString('\n')
			if err != nil {
				if err == io.EOF {
					return
				}
				os.Exit(1)
			}
			line = strings.TrimSpace(line)
			if line == "" {
				break
			}
			if strings.HasPrefix(line, "Content-Length:") {
				parts := strings.Split(line, ":")
				if len(parts) == 2 {
					fmt.Sscanf(strings.TrimSpace(parts[1]), "%d", &contentLength)
				}
			}
		}

		if contentLength == 0 {
			continue
		}

		body := make([]byte, contentLength)
		_, err := io.ReadFull(reader, body)
		if err != nil {
			os.Exit(1)
		}

		var req LspRequest
		if err := json.Unmarshal(body, &req); err != nil {
			continue
		}

		go handleLspRequest(req)
	}
}

func handleLspRequest(req LspRequest) {
	switch req.Method {
	case "initialize":
		result := InitializeResult{
			Capabilities: ServerCapabilities{
				TextDocumentSync: 1, // Full document sync
				CompletionProvider: &CompletionOptions{
					TriggerCharacters: []string{".", ":"},
				},
			},
		}
		sendLspResponse(req.Id, result, nil)

	case "initialized":
		// No-op

	case "textDocument/didOpen":
		var params TextDocumentParams
		if err := json.Unmarshal(req.Params, &params); err == nil {
			validateDocument(params.TextDocument.Uri, params.TextDocument.Text)
		}

	case "textDocument/didChange":
		var params DidChangeParams
		if err := json.Unmarshal(req.Params, &params); err == nil {
			if len(params.ContentChanges) > 0 {
				validateDocument(params.TextDocument.Uri, params.ContentChanges[0].Text)
			}
		}

	case "textDocument/completion":
		var params CompletionParams
		if err := json.Unmarshal(req.Params, &params); err == nil {
			var textContent string
			u, err := url.Parse(params.TextDocument.Uri)
			if err == nil {
				filePath := u.Path
				data, err := os.ReadFile(filePath)
				if err == nil {
					textContent = string(data)
				}
			}
			completions := getCompletions(textContent, params.Position)
			sendLspResponse(req.Id, completions, nil)
		}
	}
}

func validateDocument(uri string, text string) {
	tmpFile, err := os.CreateTemp("", "hexagen-lsp-*.hx")
	if err != nil {
		return
	}
	defer os.Remove(tmpFile.Name())
	defer tmpFile.Close()

	if _, err := tmpFile.WriteString(text); err != nil {
		return
	}
	tmpFile.Sync()

	corePath, err := findCoreBinary()
	if err != nil {
		return
	}

	cmd := exec.Command(corePath, "validate", tmpFile.Name())
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	_ = cmd.Run()

	errStr := stderr.String()
	var diagnostics []Diagnostic

	if errStr != "" {
		line := 0
		reLine := regexp.MustCompile(`line\s+(\d+)`)
		match := reLine.FindStringSubmatch(errStr)
		if len(match) == 2 {
			l, _ := strconv.Atoi(match[1])
			if l > 0 {
				line = l - 1
			}
		} else {
			reSlice := regexp.MustCompile(`slice\s+([A-Za-z0-9_]+)`)
			matchSlice := reSlice.FindStringSubmatch(errStr)
			if len(matchSlice) == 2 {
				sliceName := matchSlice[1]
				lines := strings.Split(text, "\n")
				for idx, ln := range lines {
					if strings.Contains(ln, "slice "+sliceName) {
						line = idx
						for searchIdx := idx; searchIdx < len(lines); searchIdx++ {
							searchLine := lines[searchIdx]
							if strings.Contains(searchLine, "print") || strings.Contains(searchLine, "system") || strings.Contains(searchLine, "fetch") || strings.Contains(searchLine, "curl") {
								line = searchIdx
								break
							}
							if searchIdx > idx && strings.Contains(searchLine, "slice ") {
								break
							}
						}
						break
					}
				}
			}
		}

		cleanMsg := strings.TrimSpace(errStr)
		cleanMsg = strings.TrimPrefix(cleanMsg, "Compilation Error:")
		cleanMsg = strings.TrimSpace(cleanMsg)

		diagnostics = append(diagnostics, Diagnostic{
			Range: Range{
				Start: Position{Line: line, Character: 0},
				End:   Position{Line: line, Character: 80},
			},
			Severity: 1,
			Source:   "Hexagen Compiler",
			Message:  cleanMsg,
		})
	}

	sendLspNotification("textDocument/publishDiagnostics", PublishDiagnosticsParams{
		Uri:         uri,
		Diagnostics: diagnostics,
	})
}

func getCompletions(text string, pos Position) []CompletionItem {
	var items []CompletionItem

	keywords := []string{
		"config", "database", "frontend", "css", "slice", "field", "action",
		"view", "title", "input", "button", "table", "api", "route",
		"websocket", "use", "secure", "job", "enqueue",
	}
	for _, kw := range keywords {
		items = append(items, CompletionItem{
			Label:  kw,
			Kind:   14,
			Detail: "Hexagen Keyword",
		})
	}

	types := []string{"string", "int", "float", "bool"}
	for _, t := range types {
		items = append(items, CompletionItem{
			Label:  t,
			Kind:   6,
			Detail: "Hexagen Type",
		})
	}

	if text == "" {
		return items
	}

	reSlice := regexp.MustCompile(`slice\s+([A-Za-z0-9_]+)`)
	matchesSlice := reSlice.FindAllStringSubmatch(text, -1)
	
	for _, match := range matchesSlice {
		if len(match) == 2 {
			items = append(items, CompletionItem{
				Label:  match[1],
				Kind:   7,
				Detail: "User Declared Slice",
			})
		}
	}

	lines := strings.Split(text, "\n")
	currentSlice := ""
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "slice ") {
			parts := strings.Fields(trimmed)
			if len(parts) >= 2 {
				currentSlice = strings.TrimSuffix(parts[1], "{")
				currentSlice = strings.TrimSpace(currentSlice)
			}
		} else if strings.HasPrefix(trimmed, "action ") && currentSlice != "" {
			parts := strings.Fields(trimmed)
			if len(parts) >= 2 {
				actionName := parts[1]
				if idx := strings.Index(actionName, "("); idx != -1 {
					actionName = actionName[:idx]
				}
				items = append(items, CompletionItem{
					Label:  currentSlice + "." + actionName,
					Kind:   3,
					Detail: "Slice Action Call",
				})
			}
		}
	}

	return items
}

func getPkgConfigFlags() []string {
	cmd := exec.Command("pkg-config", "--cflags", "--libs", "gtk+-3.0", "webkit2gtk-4.0")
	out, err := cmd.Output()
	if err != nil {
		cmd = exec.Command("pkg-config", "--cflags", "--libs", "gtk+-3.0", "webkit2gtk-4.1")
		out, err = cmd.Output()
		if err != nil {
			return []string{}
		}
	}
	fields := strings.Fields(strings.TrimSpace(string(out)))
	return fields
}

// addHttpFlags links OpenSSL when the generated code uses the outbound HTTP
// client (config { http: true }), detected via the emitted OpenSSL include.
func addHttpFlags(compileArgs []string, cppCode string) []string {
	if strings.Contains(cppCode, "<openssl/ssl.h>") {
		compileArgs = append(compileArgs, "-lssl", "-lcrypto")
	}
	return compileArgs
}

// addRequiredLibFlags links extra libraries declared via `config { requires: ... }`,
// detected from the emitted "// hexagen:requires ..." marker. Known aliases expand
// to canonical flags; anything else links as -l<name>.
func addRequiredLibFlags(compileArgs []string, cppCode string) []string {
	marker := "// hexagen:requires"
	idx := strings.Index(cppCode, marker)
	if idx == -1 {
		return compileArgs
	}
	line := cppCode[idx+len(marker):]
	if nl := strings.IndexByte(line, '\n'); nl != -1 {
		line = line[:nl]
	}
	for _, lib := range strings.Fields(line) {
		switch lib {
		case "ssl":
			compileArgs = append(compileArgs, "-lssl", "-lcrypto")
		case "curl":
			compileArgs = append(compileArgs, "-lcurl")
		case "crypto":
			compileArgs = append(compileArgs, "-lcrypto")
		case "zlib", "z":
			compileArgs = append(compileArgs, "-lz")
		default:
			compileArgs = append(compileArgs, "-l"+lib)
		}
	}
	return compileArgs
}

func addModuleFlags(compileArgs []string) []string {
	if _, err := os.Stat(".hexagen_modules"); err == nil {
		filepath.Walk(".hexagen_modules", func(path string, info fs.FileInfo, err error) error {
			if err != nil {
				return err
			}
			if info.IsDir() {
				compileArgs = append(compileArgs, "-I"+path)
			} else if strings.HasSuffix(path, ".cpp") {
				compileArgs = append(compileArgs, path)
			}
			return nil
		})
	}
	return compileArgs
}

func downloadFile(url string, destPath string) error {
	resp, err := http.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("bad status: %s", resp.Status)
	}
	err = os.MkdirAll(filepath.Dir(destPath), 0755)
	if err != nil {
		return err
	}
	out, err := os.Create(destPath)
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, resp.Body)
	return err
}

func handleAdd(packageName string) {
	fmt.Printf("📦 Installing package '%s'...\n", packageName)
	destDir := filepath.Join(".hexagen_modules", packageName)
	err := os.MkdirAll(destDir, 0755)
	if err != nil {
		fmt.Printf("❌ Error creating modules directory: %v\n", err)
		os.Exit(1)
	}

	switch packageName {
	case "json":
		url := "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp"
		destFile := filepath.Join(destDir, "json.hpp")
		err = downloadFile(url, destFile)
	case "jwt":
		url := "https://raw.githubusercontent.com/Thalhammer/jwt-cpp/master/include/jwt-cpp/jwt.h"
		destFile := filepath.Join(destDir, "jwt.h")
		err = downloadFile(url, destFile)
	case "smtp":
		destFile := filepath.Join(destDir, "smtp.hpp")
		smtpContent := `// Self-contained simple SMTP client in C++
#pragma once
#include <string>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <algorithm>

class SMTPClient {
public:
    static bool sendEmail(const std::string& host, int port, const std::string& from, const std::string& to, const std::string& subject, const std::string& body) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        struct hostent* server = gethostbyname(host.c_str());
        if (!server) { close(sock); return false; }
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        std::copy((char*)server->h_addr, (char*)server->h_addr + server->h_length, (char*)&addr.sin_addr.s_addr);
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(sock); return false; }
        
        auto read_resp = [sock]() {
            char buf[1024];
            int n = recv(sock, buf, 1023, 0);
            if (n > 0) { buf[n] = '\0'; }
        };
        
        auto send_cmd = [sock, read_resp](const std::string& cmd) {
            send(sock, cmd.c_str(), cmd.length(), 0);
            read_resp();
        };

        read_resp();
        send_cmd("HELO localhost\r\n");
        send_cmd("MAIL FROM:<" + from + ">\r\n");
        send_cmd("RCPT TO:<" + to + ">\r\n");
        send_cmd("DATA\r\n");
        std::stringstream msg;
        msg << "From: " << from << "\r\n"
            << "To: " << to << "\r\n"
            << "Subject: " << subject << "\r\n\r\n"
            << body << "\r\n.\r\n";
        send_cmd(msg.str());
        send_cmd("QUIT\r\n");
        close(sock);
        return true;
    }
};
`
		err = os.WriteFile(destFile, []byte(smtpContent), 0644)
	default:
		fmt.Printf("❌ Unknown package: %s. Supported: json, jwt, smtp\n", packageName)
		os.Exit(1)
	}

	if err != nil {
		fmt.Printf("❌ Failed to install package: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("✅ Package '%s' installed successfully under .hexagen_modules/%s/\n", packageName, packageName)
}

