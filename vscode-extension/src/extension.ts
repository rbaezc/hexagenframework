import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions
} from 'vscode-languageclient/node';

let client: LanguageClient;
let logChannel: vscode.OutputChannel;

function getExecutablePath(): string {
    logChannel.appendLine("Resolving Hexagen executable path...");
    const config = vscode.workspace.getConfiguration('hexagen');
    const configPath = config.get<string>('executablePath');
    logChannel.appendLine(`Configuration hexagen.executablePath: "${configPath || ''}"`);

    if (configPath) {
        if (path.isAbsolute(configPath)) {
            logChannel.appendLine(`Checking absolute path: ${configPath}`);
            if (fs.existsSync(configPath)) {
                logChannel.appendLine(`Found absolute path: ${configPath}`);
                return configPath;
            }
            logChannel.appendLine(`Absolute path does not exist: ${configPath}`);
        } else {
            const folders = vscode.workspace.workspaceFolders;
            logChannel.appendLine(`Checking relative path "${configPath}" against ${folders ? folders.length : 0} workspace folders`);
            if (folders) {
                for (const folder of folders) {
                    const resolved = path.resolve(folder.uri.fsPath, configPath);
                    logChannel.appendLine(`Resolved relative path: ${resolved}`);
                    if (fs.existsSync(resolved)) {
                        logChannel.appendLine(`Found resolved path: ${resolved}`);
                        return resolved;
                    }
                }
            }
        }
    }

    const folders = vscode.workspace.workspaceFolders;
    logChannel.appendLine(`Auto-detecting hf in ${folders ? folders.length : 0} workspace folders`);
    if (folders) {
        for (const folder of folders) {
            logChannel.appendLine(`Scanning folder: ${folder.uri.fsPath}`);
            const rootPath = path.join(folder.uri.fsPath, 'hf');
            logChannel.appendLine(`Checking rootPath: ${rootPath}`);
            if (fs.existsSync(rootPath)) {
                logChannel.appendLine(`Found hf at: ${rootPath}`);
                return rootPath;
            }
            const subPath = path.join(folder.uri.fsPath, 'hexagen_framework', 'hf');
            logChannel.appendLine(`Checking subPath: ${subPath}`);
            if (fs.existsSync(subPath)) {
                logChannel.appendLine(`Found hf at: ${subPath}`);
                return subPath;
            }
        }
    }

    logChannel.appendLine("Fallback to 'hf' (system PATH)");
    return 'hf';
}

export function activate() {
    logChannel = vscode.window.createOutputChannel("Hexagen Extension Debug");
    logChannel.show(true);
    logChannel.appendLine("Activating Hexagen Extension...");

    const executablePath = getExecutablePath();
    logChannel.appendLine(`Selected executable path: "${executablePath}"`);

    // LSP Server Options - Spawn "hf lsp"
    const serverOptions: ServerOptions = {
        run: { command: executablePath, args: ['lsp'] },
        debug: { command: executablePath, args: ['lsp'] }
    };

    // Client Options
    const clientOptions: LanguageClientOptions = {
        // Register server for .hx files
        documentSelector: [{ scheme: 'file', language: 'hexagen' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.hx')
        }
    };

    // Create and start the LSP client
    client = new LanguageClient(
        'hexagenLanguageServer',
        'Hexagen Language Server',
        serverOptions,
        clientOptions
    );

    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
