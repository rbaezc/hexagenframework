package main

import (
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"
)

func printUsage() {
	fmt.Println("Hexagen Framework CLI (hf)")
	fmt.Println("Usage:")
	fmt.Println("  hf new <project_name>               - Scaffold a new project workspace")
	fmt.Println("  hf dev [file_or_dir]                - Start live dev server (Hot Reload) (default: .)")
	fmt.Println("  hf start [file_or_dir]              - Compile and run in production mode (default: .)")
	fmt.Println("  hf transpile [file_or_dir]          - Transpile to C++ source code (default: .)")
	fmt.Println("  hf compile [file_or_dir] -o <out>   - Transpile and compile to executable (default: .)")
	fmt.Println("  hf run [file_or_dir]                - Transpile, compile, and run immediately (default: .)")
	fmt.Println("  hf ast [file_or_dir]                - Print the AST of the input source (default: .)")
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
		compileCmd := exec.Command("g++", "-std=c++17", tempCppFile, "-o", outputExe, "-pthread")
		compileCmd.Stdout = os.Stdout
		compileCmd.Stderr = os.Stderr
		err = compileCmd.Run()
		if err != nil {
			fmt.Println("❌ Compilation failed. Fix syntax errors to reload.")
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
		compileCmd := exec.Command("g++", "-std=c++17", tempCppFile, "-o", outputExe, "-pthread")
		compileCmd.Stdout = os.Stdout
		compileCmd.Stderr = os.Stderr
		err = compileCmd.Run()
		if err != nil {
			fmt.Println("Error: C++ compilation failed.")
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
