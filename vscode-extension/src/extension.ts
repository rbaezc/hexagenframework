import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate() {
    // LSP Server Options - Spawn "hf lsp"
    const serverOptions: ServerOptions = {
        run: { command: 'hf', args: ['lsp'] },
        debug: { command: 'hf', args: ['lsp'] }
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
