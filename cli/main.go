package main

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"
)

const Version = "v0.3.0"

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
	output, err := cmd.CombinedOutput()
	if err != nil {
		return string(output), fmt.Errorf("hf_core execution failed: %v\nOutput: %s", err, string(output))
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
		compileArgs := []string{"-std=c++17", tempCppFile, "-o", outputExe, "-pthread"}
		if strings.Contains(cppCode, "Database Engine: sqlite") {
			compileArgs = append(compileArgs, "-lsqlite3")
		} else if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
			compileArgs = append(compileArgs, "-lpq")
		} else if strings.Contains(cppCode, "Database Engine: mysql") {
			compileArgs = append(compileArgs, "-lmysqlclient")
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
		compileArgs := []string{"-std=c++17", tempCppFile, "-o", outputExe, "-pthread"}
		if strings.Contains(cppCode, "Database Engine: sqlite") {
			compileArgs = append(compileArgs, "-lsqlite3")
		} else if strings.Contains(cppCode, "Database Engine: postgres") || strings.Contains(cppCode, "Database Engine: postgresql") {
			compileArgs = append(compileArgs, "-lpq")
		} else if strings.Contains(cppCode, "Database Engine: mysql") {
			compileArgs = append(compileArgs, "-lmysqlclient")
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
