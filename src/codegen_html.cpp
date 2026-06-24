// Frontend (HTML / React / Admin) code generation. Split out of codegen.cpp
// (god-file reduction). Behavior-preserving: emission logic moved verbatim.
#include "codegen.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

std::string CodeGenerator::generateHTMLContent(std::shared_ptr<ASTView> view) {
    bool hasHtml = false;
    for (const auto& elem : view->elements) {
        if (elem->type == "html") {
            hasHtml = true;
            break;
        }
    }
    std::string customStyle = "";
    if (std::filesystem::exists("style.css")) {
        std::ifstream f("style.css");
        if (f.is_open()) {
            std::stringstream buffer;
            buffer << f.rdbuf();
            customStyle = buffer.str();
            f.close();
        }
    }
    std::stringstream ss;
    ss << "<!DOCTYPE html>\n<html lang=\"es\">\n<head>\n"
       << "    <meta charset=\"UTF-8\">\n"
       << "    <title>";
    std::string titleText = "Hexagen App";
    for (const auto& elem : view->elements) {
        if (elem->type == "title") titleText = elem->label;
    }
    ss << titleText << "</title>\n"
       << "    <link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">\n"
       << "    <link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>\n"
       << "    <link href=\"https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&family=JetBrains+Mono:wght@400;700&display=swap\" rel=\"stylesheet\">\n"
       << "    <style>\n";
    if (program->css != "none") {
        ss << "        :root {\n"
           << "            --bg-color: #0b0f19;\n"
           << "            --card-bg: rgba(20, 30, 55, 0.45);\n"
           << "            --border-color: rgba(255, 255, 255, 0.08);\n"
           << "            --primary-glow: #00f2fe;\n"
           << "            --secondary-glow: #4facfe;\n"
           << "            --text-color: #f3f4f6;\n"
           << "            --text-muted: #9ca3af;\n"
           << "        }\n"
           << "        * { box-sizing: border-box; margin: 0; padding: 0; }\n"
           << "        body {\n"
           << "            font-family: 'Outfit', sans-serif; background-color: var(--bg-color); color: var(--text-color);\n"
           << "            min-height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; overflow-x: hidden; position: relative;\n"
           << "        }\n"
           << "        body::before {\n"
           << "            content: ''; position: absolute; width: 300px; height: 300px;\n"
           << "            background: radial-gradient(circle, var(--primary-glow) 0%, transparent 70%);\n"
           << "            top: 10%; left: 15%; opacity: 0.15; filter: blur(80px); z-index: 0;\n"
           << "        }\n"
           << "        body::after {\n"
           << "            content: ''; position: absolute; width: 350px; height: 350px;\n"
           << "            background: radial-gradient(circle, var(--secondary-glow) 0%, transparent 70%);\n"
           << "            bottom: 15%; right: 15%; opacity: 0.15; filter: blur(80px); z-index: 0;\n"
           << "        }\n"
           << "        .container { width: 100%; max-width: 550px; padding: 2rem; z-index: 1; }\n"
           << "        .card {\n"
           << "            background: var(--card-bg); backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px);\n"
           << "            border: 1px solid var(--border-color); border-radius: 24px; padding: 2.5rem; box-shadow: 0 20px 50px rgba(0, 0, 0, 0.3);\n"
           << "        }\n"
           << "        .heading-container { margin-bottom: 2rem; text-align: center; }\n"
           << "        .main-heading { font-size: 2rem; font-weight: 800; background: linear-gradient(135deg, #fff 0%, #a5b4fc 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent; margin-bottom: 0.5rem; }\n"
           << "        .sub-heading { font-size: 0.95rem; color: var(--text-muted); }\n"
           << "        .form-group { margin-bottom: 1.5rem; }\n"
           << "        .form-label { display: block; font-size: 0.85rem; font-weight: 600; text-transform: uppercase; color: var(--text-muted); margin-bottom: 0.5rem; }\n"
           << "        .form-input { width: 100%; background: rgba(255, 255, 255, 0.03); border: 1px solid var(--border-color); border-radius: 12px; padding: 0.85rem 1rem; color: white; font-family: inherit; font-size: 1rem; }\n"
           << "        .form-input:focus { outline: none; border-color: var(--primary-glow); background: rgba(255, 255, 255, 0.06); }\n"
           << "        .btn {\n"
           << "            width: 100%; background: linear-gradient(135deg, var(--secondary-glow) 0%, var(--primary-glow) 100%);\n"
           << "            border: none; color: #0b0f19; padding: 1rem; font-size: 1rem; font-weight: 700; border-radius: 12px; cursor: pointer; transition: all 0.3s ease; margin-bottom: 1rem;\n"
           << "        }\n"
           << "        .btn:hover { transform: translateY(-2px); filter: brightness(1.1); }\n"
           << "        .result-panel { margin-top: 2rem; background: rgba(0, 0, 0, 0.25); border-radius: 16px; border: 1px solid rgba(255, 255, 255, 0.05); padding: 1.25rem; display: none; }\n"
           << "        .result-title { font-size: 0.85rem; font-weight: 600; color: var(--primary-glow); margin-bottom: 0.5rem; text-transform: uppercase; }\n"
           << "        .result-code { font-family: 'JetBrains Mono', monospace; font-size: 0.85rem; white-space: pre-wrap; color: #e5e7eb; }\n"
           << "        .table-container { margin-top: 2rem; background: rgba(0, 0, 0, 0.2); border-radius: 12px; overflow: hidden; border: 1px solid var(--border-color); }\n"
           << "        .data-table { width: 100%; text-align: left; border-collapse: collapse; }\n"
           << "        .data-table th, .data-table td { padding: 0.75rem 1rem; border-bottom: 1px solid var(--border-color); }\n"
           << "        .data-table th { background: rgba(255, 255, 255, 0.03); font-size: 0.85rem; text-transform: uppercase; color: var(--text-muted); }\n"
           << "        .data-table td { font-size: 0.95rem; }\n";
    }
    if (!customStyle.empty()) {
        ss << "        /* Custom Styles Overrides */\n" << customStyle << "\n";
    }
    ss << "    </style>\n"
       << "</head>\n"
       << "<body>\n";

    if (program->css != "none") {
        ss << "    <main class=\"container\">\n"
           << "        <section class=\"card\">\n"
           << "            <div id=\"hexa-root\">\n";
        
        ss << "                <div class=\"heading-container\">\n";
        ss << "                    <h1 class=\"main-heading\">" << view->name << "</h1>\n";
        
        std::string sub = "Hexagen Compiled UI";
        for (const auto& elem : view->elements) {
            if (elem->type == "title") sub = elem->label;
        }
        ss << "                    <p class=\"sub-heading\">" << sub << "</p>\n";
        ss << "                </div>\n";
    } else {
        ss << "            <div id=\"hexa-root\">\n";
    }

    for (const auto& elem : view->elements) {
        if (elem->type == "input") {
            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " class=\"" + elem->className + "\"";
                ss << "                <div" << cls << ">\n";
                ss << "                    <label" << cls << ">" << elem->label << "</label>\n";
                ss << "                    <input type=\"text\"" << cls << " id=\"input-" << elem->name << "\" name=\"" << elem->name << "\">\n";
                ss << "                </div>\n";
            } else {
                ss << "                <div class=\"form-group\">\n";
                ss << "                    <label class=\"form-label\">" << elem->label << "</label>\n";
                ss << "                    <input type=\"text\" class=\"form-input\" id=\"input-" << elem->name << "\" name=\"" << elem->name << "\">\n";
                ss << "                </div>\n";
            }
        } else if (elem->type == "button") {
            // Check if redirect / navigation button
            bool isNavigationView = false;
            std::string viewTarget = "";
            for (const auto& v : program->views) {
                if (v->name == elem->targetAction) {
                    isNavigationView = true;
                    viewTarget = v->name;
                    std::transform(viewTarget.begin(), viewTarget.end(), viewTarget.begin(), ::tolower);
                    break;
                }
            }

            std::string clickAttr = "";
            if (isNavigationView) {
                clickAttr = "onclick=\"window.location.href = '/" + viewTarget + "'\"";
            } else {
                std::string apiEndpoint = "/execute";
                if (!program->apis.empty()) {
                    for (const auto& r : program->allRoutes()) {
                        if (r->targetAction == elem->targetAction) {
                            apiEndpoint = r->path;
                            break;
                        }
                    }
                }
                clickAttr = "onclick=\"triggerAction('" + apiEndpoint + "')\"";
            }

            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " class=\"" + elem->className + "\"";
                ss << "                <button" << cls << " " << clickAttr << ">" << elem->label << "</button>\n";
            } else {
                ss << "                <button class=\"btn\" " << clickAttr << ">" << elem->label << "</button>\n";
            }
        } else if (elem->type == "table") {
            // Add action column if there is delete route
            bool hasDeleteRoute = false;
            std::string deleteEndpoint = "";
            if (!program->apis.empty()) {
                for (const auto& r : program->allRoutes()) {
                    if (r->method == "DELETE") {
                        size_t dotPos = r->targetAction.find('.');
                        std::string targetSlice = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                        if (targetSlice == elem->label) {
                            hasDeleteRoute = true;
                            deleteEndpoint = r->path;
                            break;
                        }
                    }
                }
            }

            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " class=\"" + elem->className + "\"";
                ss << "                <div" << cls << ">\n";
                ss << "                    <table>\n";
                ss << "                        <thead>\n";
                ss << "                            <tr>\n";
                for (const auto& col : elem->columns) {
                    ss << "                                <th>" << col << "</th>\n";
                }
                if (hasDeleteRoute) {
                    ss << "                                <th>Acciones</th>\n";
                }
                ss << "                            </tr>\n";
                ss << "                        </thead>\n";
                ss << "                        <tbody id=\"table-body-" << elem->label << "\">\n";
                ss << "                            <!-- dynamic rows -->\n";
                ss << "                        </tbody>\n";
                ss << "                    </table>\n";
                ss << "                </div>\n";
            } else {
                ss << "                <div class=\"table-container\">\n";
                ss << "                    <table class=\"data-table\">\n";
                ss << "                        <thead>\n";
                ss << "                            <tr>\n";
                for (const auto& col : elem->columns) {
                    ss << "                                <th>" << col << "</th>\n";
                }
                if (hasDeleteRoute) {
                    ss << "                                <th>Acciones</th>\n";
                }
                ss << "                            </tr>\n";
                ss << "                        </thead>\n";
                ss << "                        <tbody id=\"table-body-" << elem->label << "\">\n";
                ss << "                            <!-- dynamic rows -->\n";
                ss << "                        </tbody>\n";
                ss << "                    </table>\n";
                ss << "                </div>\n";
            }
        } else if (elem->type == "html") {
            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " class=\"" + elem->className + "\"";
                ss << "                <div" << cls << ">" << elem->label << "</div>\n";
            } else {
                ss << elem->label << "\n";
            }
        }
    }

    ss << "            </div>\n";
    if (program->css != "none") {
        ss << "            <div class=\"result-panel\" id=\"result-panel\">\n"
           << "                <div class=\"result-title\" id=\"result-title\">Respuesta de la API C++</div>\n"
           << "                <pre class=\"result-code\"><code id=\"result-code\"></code></pre>\n"
           << "            </div>\n"
           << "        </section>\n"
           << "    </main>\n";
    } else {
        ss << "            <div id=\"result-panel\" style=\"display:none;\">\n"
           << "                <div id=\"result-title\">Respuesta de la API C++</div>\n"
           << "                <pre><code id=\"result-code\"></code></pre>\n"
           << "            </div>\n";
    }
    ss << "    <script>\n";

    // Refresh dynamic tables script
    ss << "        async function refreshTables() {\n";
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            bool hasDeleteRoute = false;
            std::string deleteEndpoint = "";
            if (!program->apis.empty()) {
                for (const auto& r : program->allRoutes()) {
                    if (r->method == "DELETE") {
                        size_t dotPos = r->targetAction.find('.');
                        std::string targetSlice = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                        if (targetSlice == elem->label) {
                            hasDeleteRoute = true;
                            deleteEndpoint = r->path;
                            break;
                        }
                    }
                }
            }
            ss << "            try {\n";
            ss << "                const response = await fetch('/api/" << elem->label << "');\n";
            ss << "                const data = await response.json();\n";
            ss << "                const tbody = document.getElementById('table-body-" << elem->label << "');\n";
            ss << "                if (tbody) {\n";
            ss << "                    tbody.innerHTML = '';\n";
            ss << "                    data.forEach(row => {\n";
            ss << "                        const tr = document.createElement('tr');\n";
            ss << "                        let rowHtml = '';\n";
            for (const auto& col : elem->columns) {
                ss << "                        rowHtml += `<td>${row." << col << " || ''}</td>`;\n";
            }
            if (hasDeleteRoute) {
                std::string keyCol = elem->columns.empty() ? "" : elem->columns[0];
                ss << "                        rowHtml += `<td><button class=\"btn\" style=\"padding:0.4rem 0.8rem; font-size:0.8rem; margin:0; width:auto; background:linear-gradient(135deg, #f43f5e 0%, #e11d48 100%); color:white;\" onclick=\"deleteRow('${row." << keyCol << "}', '" << deleteEndpoint << "')\">Eliminar</button></td>`;\n";
            }
            ss << "                        tr.innerHTML = rowHtml;\n";
            ss << "                        tbody.appendChild(tr);\n";
            ss << "                    });\n";
            ss << "                }\n";
            ss << "            } catch (err) {}\n";
        }
    }
    ss << "        }\n\n";

    // Delete row function
    ss << "        async function deleteRow(idValue, endpoint) {\n";
    ss << "            if (!confirm('¿Seguro que deseas eliminar este registro?')) return;\n";
    ss << "            const payload = {};\n";
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            std::string firstFieldName = "";
            for (const auto& slice : program->slices) {
                if (slice->name == elem->label) {
                    if (!slice->fields.empty()) {
                        firstFieldName = slice->fields[0]->name;
                    }
                }
            }
            if (firstFieldName.empty() && !elem->columns.empty()) {
                firstFieldName = elem->columns[0];
            }
            ss << "            payload['" << firstFieldName << "'] = idValue;\n";
        }
    }
    ss << "            try {\n";
    ss << "                const response = await fetch(endpoint, {\n";
    ss << "                    method: 'DELETE',\n";
    ss << "                    headers: {\n";
    ss << "                        'Content-Type': 'application/json',\n";
    ss << "                        'Authorization': 'Bearer ' + (localStorage.getItem('hexagen_token') || 'hexagen_token_123')\n";
    ss << "                    },\n";
    ss << "                    body: JSON.stringify(payload)\n";
    ss << "                });\n";
    ss << "                const data = await response.json();\n";
    ss << "                document.getElementById('result-code').innerText = JSON.stringify(data, null, 2);\n";
    ss << "                document.getElementById('result-panel').style.display = 'block';\n";
    ss << "                refreshTables();\n";
    ss << "            } catch (err) {\n";
    ss << "                alert('Error al eliminar el registro');\n";
    ss << "            }\n";
    ss << "        }\n\n";

    ss << "        async function triggerAction(endpoint) {\n"
       << "            const payload = {};\n"
       << "            document.querySelectorAll('.form-input').forEach(input => {\n"
       << "                payload[input.name] = input.value;\n"
       << "            });\n"
       << "            try {\n"
       << "                const response = await fetch(endpoint, {\n"
       << "                    method: 'POST',\n"
       << "                    headers: { \n"
       << "                        'Content-Type': 'application/json',\n"
       << "                        'Authorization': 'Bearer ' + (localStorage.getItem('hexagen_token') || 'hexagen_token_123')\n"
       << "                    },\n"
       << "                    body: JSON.stringify(payload)\n"
       << "                });\n"
       << "                const data = await response.json();\n"
       << "                document.getElementById('result-code').innerText = JSON.stringify(data, null, 2);\n"
       << "                document.getElementById('result-panel').style.display = 'block';\n"
       << "                refreshTables();\n"
       << "            } catch (err) {\n"
       << "                document.getElementById('result-code').innerText = 'Error connecting to API server.';\n"
       << "                document.getElementById('result-panel').style.display = 'block';\n"
       << "            }\n"
       << "        }\n";
    
    ss << "        // LiveView Client-side Script (Phase 2)\n"
       << "        const liveSocket = new WebSocket('ws://' + window.location.host + '/live');\n"
       << "        liveSocket.onmessage = function(event) {\n"
       << "            try {\n"
       << "                const msg = JSON.parse(event.data);\n"
       << "                if (msg.type === 'patch' && Array.isArray(msg.patches)) {\n"
       << "                    // LiveView DOM patching: apply minimal server-computed diffs\n"
       << "                    msg.patches.forEach(p => {\n"
       << "                        const el = document.querySelector('[hg-id=\"' + p.id + '\"]');\n"
       << "                        if (el && el.innerHTML !== p.html) el.innerHTML = p.html;\n"
       << "                    });\n"
       << "                } else if (msg.event === 'action') {\n"
       << "                    refreshTables();\n"
       << "                } else if (msg.event === 'input_change') {\n"
       << "                    const input = document.getElementById('input-' + msg.field);\n"
       << "                    if (input && input.value !== msg.value) {\n"
       << "                        input.value = msg.value;\n"
       << "                    }\n"
       << "                }\n"
       << "            } catch(e) {}\n"
       << "        };\n"
       << "        function setupLiveEvents() {\n"
       << "            document.querySelectorAll('.form-input').forEach(input => {\n"
       << "                input.addEventListener('input', () => {\n"
       << "                    liveSocket.send(JSON.stringify({\n"
       << "                        event: 'input_change',\n"
       << "                        field: input.name,\n"
       << "                        value: input.value\n"
       << "                    }));\n"
       << "                });\n"
       << "            });\n"
       << "        }\n"
       << "        liveSocket.onopen = setupLiveEvents;\n\n";
     
    ss << "        window.onload = () => { refreshTables(); setupLiveEvents(); };\n"
       << "    </script>\n"
       << "</body>\n"
       << "</html>\n";

    return ss.str();
}

void CodeGenerator::generateReactFrontend() {
    namespace fs = std::filesystem;
    try {
        fs::create_directories("frontend/src/pages");
    } catch (...) {
        std::cerr << "Error creating directory frontend/src/pages" << std::endl;
        return;
    }

    // package.json
    {
        std::ofstream file("frontend/package.json");
        if (file.is_open()) {
            file << R"JSON({
  "name": "hexagen-frontend",
  "private": true,
  "version": "0.0.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "preview": "vite preview"
  },
  "dependencies": {
    "react": "^18.2.0",
    "react-dom": "^18.2.0",
    "react-router-dom": "^6.22.3"
  },
  "devDependencies": {
    "@types/react": "^18.2.66",
    "@types/react-dom": "^18.2.22",
    "@vitejs/plugin-react": "^4.2.1",
    "autoprefixer": "^10.4.19",
    "postcss": "^8.4.38",
    "tailwindcss": "^3.4.1",
    "typescript": "^5.2.2",
    "vite": "^5.2.0"
  }
})JSON";
            file.close();
        }
    }

    // tsconfig.json
    {
        std::ofstream file("frontend/tsconfig.json");
        if (file.is_open()) {
            file << R"JSON({
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "lib": ["DOM", "DOM.Iterable", "ES2020"],
    "module": "ESNext",
    "skipLibCheck": true,
    "moduleResolution": "bundler",
    "allowImportingTsExtensions": true,
    "resolveJsonModule": true,
    "isolatedModules": true,
    "noEmit": true,
    "jsx": "react-jsx",
    "strict": true,
    "noUnusedLocals": false,
    "noUnusedParameters": false,
    "noFallthroughCasesInSwitch": true
  },
  "include": ["src"]
})JSON";
            file.close();
        }
    }

    // vite.config.ts
    {
        std::ofstream file("frontend/vite.config.ts");
        if (file.is_open()) {
            file << R"TS(import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 3000,
    proxy: {
      '/api': 'http://localhost:8080'
    }
  }
});
)TS";
            file.close();
        }
    }

    // tailwind.config.js
    {
        std::ofstream file("frontend/tailwind.config.js");
        if (file.is_open()) {
            file << R"JS(/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {},
  },
  plugins: [],
}
)JS";
            file.close();
        }
    }

    // postcss.config.js
    {
        std::ofstream file("frontend/postcss.config.js");
        if (file.is_open()) {
            file << R"JS(export default {
  plugins: {
    tailwindcss: {},
    autoprefixer: {},
  },
}
)JS";
            file.close();
        }
    }

    // index.html
    {
        std::ofstream file("frontend/index.html");
        if (file.is_open()) {
            file << R"HTML(<!DOCTYPE html>
<html lang="es">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Hexagen React App</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/main.tsx"></script>
  </body>
</html>
)HTML";
            file.close();
        }
    }

    // src/main.tsx
    {
        std::ofstream file("frontend/src/main.tsx");
        if (file.is_open()) {
            file << R"TS(import React from 'react';
import ReactDOM from 'react-dom/client';
import App from './App';
import './index.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
)TS";
            file.close();
        }
    }

    // src/index.css
    {
        std::ofstream file("frontend/src/index.css");
        if (file.is_open()) {
            file << R"CSS(@tailwind base;
@tailwind components;
@tailwind utilities;

:root {
    --bg-color: #0b0f19;
    --card-bg: rgba(20, 30, 55, 0.45);
    --border-color: rgba(255, 255, 255, 0.08);
    --primary-glow: #00f2fe;
    --secondary-glow: #4facfe;
    --text-color: #f3f4f6;
    --text-muted: #9ca3af;
}
)CSS";
            // Check if local style.css exists in the directory where hf was run, and merge it
            if (std::filesystem::exists("style.css")) {
                std::ifstream custom("style.css");
                if (custom.is_open()) {
                    file << "\n/* Custom Styles Overrides */\n";
                    file << custom.rdbuf();
                    custom.close();
                }
            }
            file.close();
        }
    }

    // src/App.tsx
    {
        std::ofstream file("frontend/src/App.tsx");
        if (file.is_open()) {
            file << "import { BrowserRouter as Router, Routes, Route } from 'react-router-dom';\n";
            for (const auto& view : program->views) {
                file << "import " << view->name << " from './pages/" << view->name << "';\n";
            }
            file << "\nexport default function App() {\n";
            file << "    return (\n";
            file << "        <Router>\n";
            file << "            <Routes>\n";
            if (!program->views.empty()) {
                file << "                <Route path=\"/\" element={<" << program->views[0]->name << " />} />\n";
            }
            for (const auto& view : program->views) {
                std::string pathLower = view->name;
                std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);
                file << "                <Route path=\"/" << pathLower << "\" element={<" << view->name << " />} />\n";
            }
            file << "            </Routes>\n";
            file << "        </Router>\n";
            file << "    );\n";
            file << "}\n";
            file.close();
        }
    }

    // Generate each view
    for (const auto& view : program->views) {
        generateReactPage(view);
    }

    std::cerr << "[Hexagen] Building React frontend..." << std::endl;
    if (!fs::exists("frontend/node_modules")) {
        std::cerr << "[Hexagen] Installing frontend dependencies (npm install)..." << std::endl;
        std::system("cd frontend && npm install >&2");
    }
    std::system("cd frontend && npm run build >&2");
}

void CodeGenerator::generateReactPage(std::shared_ptr<ASTView> view) {
    std::ofstream file("frontend/src/pages/" + view->name + ".tsx");
    if (!file.is_open()) return;

    file << "import React, { useState, useEffect } from 'react';\n";
    file << "import { useNavigate } from 'react-router-dom';\n\n";
    file << "export default function " << view->name << "() {\n";
    file << "    const navigate = useNavigate();\n";

    // Detect inputs and create state variables
    for (const auto& elem : view->elements) {
        if (elem->type == "input") {
            file << "    const [" << elem->name << ", set" << elem->name << "] = useState('');\n";
        }
    }

    // Detect tables and create state variables and fetch triggers
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            file << "    const [" << elem->label << "Rows, set" << elem->label << "Rows] = useState<any[]>([]);\n";
            file << "    const refresh" << elem->label << " = async () => {\n";
            file << "        try {\n";
            file << "            const res = await fetch('/api/" << elem->label << "');\n";
            file << "            const data = await res.json();\n";
            file << "            set" << elem->label << "Rows(data);\n";
            file << "        } catch(err) {}\n";
            file << "    };\n";
        }
    }

    // Global result block state
    file << "    const [result, setResult] = useState<any>(null);\n\n";

    // useEffect to populate tables on mount
    file << "    useEffect(() => {\n";
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            file << "        refresh" << elem->label << "();\n";
        }
    }
    file << "    }, []);\n\n";

    // Handle action click functions
    for (const auto& elem : view->elements) {
        if (elem->type == "button") {
            size_t dotPos = elem->targetAction.find('.');
            if (dotPos != std::string::npos) {
                std::string sliceName = elem->targetAction.substr(0, dotPos);
                std::string actionName = elem->targetAction.substr(dotPos + 1);
                
                file << "    const handle" << sliceName << "_" << actionName << " = async () => {\n";
                file << "        const payload = {\n";
                for (const auto& inputElem : view->elements) {
                    if (inputElem->type == "input") {
                        bool isNum = false;
                        for (const auto& s : program->slices) {
                            if (s->name == sliceName) {
                                for (const auto& f : s->fields) {
                                    if (f->name == inputElem->name && (f->type == DataType::INT || f->type == DataType::FLOAT || f->type == DataType::RELATION)) {
                                        isNum = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (isNum) {
                            file << "            " << inputElem->name << ": Number(" << inputElem->name << ") || 0,\n";
                        } else {
                            file << "            " << inputElem->name << ": " << inputElem->name << ",\n";
                        }
                    }
                }
                file << "        };\n";
                file << "        try {\n";
                file << "            const res = await fetch('/api/" << sliceName << "', {\n";
                file << "                method: 'POST',\n";
                file << "                headers: { 'Content-Type': 'application/json' },\n";
                file << "                body: JSON.stringify(payload)\n";
                file << "            });\n";
                file << "            const data = await res.json();\n";
                file << "            setResult(data);\n";
                for (const auto& tbl : view->elements) {
                    if (tbl->type == "table") {
                        file << "            refresh" << tbl->label << "();\n";
                    }
                }
                file << "        } catch(err) {}\n";
                file << "    };\n\n";
            }
        }
    }

    // Delete actions
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            bool hasDeleteRoute = false;
            std::string deleteEndpoint = "";
            if (!program->apis.empty()) {
                for (const auto& r : program->allRoutes()) {
                    if (r->method == "DELETE") {
                        size_t dotPos = r->targetAction.find('.');
                        std::string targetSlice = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                        if (targetSlice == elem->label) {
                            hasDeleteRoute = true;
                            deleteEndpoint = r->path;
                            break;
                        }
                    }
                }
            }
            if (hasDeleteRoute) {
                std::string keyCol = elem->columns.empty() ? "id" : elem->columns[0];
                file << "    const handleDelete_" << elem->label << " = async (idVal: any) => {\n";
                file << "        if (!confirm('¿Seguro que deseas eliminar este registro?')) return;\n";
                file << "        try {\n";
                file << "            const res = await fetch('/api/" << elem->label << "', {\n";
                file << "                method: 'DELETE',\n";
                file << "                headers: { 'Content-Type': 'application/json' },\n";
                file << "                body: JSON.stringify({ " << keyCol << ": idVal })\n";
                file << "            });\n";
                file << "            refresh" << elem->label << "();\n";
                file << "        } catch(err) {}\n";
                file << "    };\n\n";
            }
        }
    }    // Render component
    file << "    return (\n";
    if (program->css == "none") {
        file << "        <>\n";
        for (const auto& elem : view->elements) {
            if (elem->type == "input") {
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "            <div className=\"max-w-md mx-auto mb-4\">\n";
                file << "                <label className=\"block text-xs font-semibold uppercase text-gray-400 mb-2\">" << elem->name << "</label>\n";
                file << "                <input type=\"text\" value={" << elem->name << "} onChange={(e) => set" << elem->name << "(e.target.value)}" << cls << " />\n";
                file << "            </div>\n";
            } else if (elem->type == "button") {
                size_t dotPos = elem->targetAction.find('.');
                std::string clickHandler = "";
                if (dotPos != std::string::npos) {
                    std::string sliceName = elem->targetAction.substr(0, dotPos);
                    std::string actionName = elem->targetAction.substr(dotPos + 1);
                    clickHandler = "handle" + sliceName + "_" + actionName;
                } else {
                    std::string pathLower = elem->targetAction;
                    std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);
                    clickHandler = "() => navigate('/" + pathLower + "')";
                }
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "            <div className=\"max-w-md mx-auto mb-4\">\n";
                file << "                <button onClick={" << clickHandler << "}" << cls << ">" << elem->label << "</button>\n";
                file << "            </div>\n";
            } else if (elem->type == "html") {
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "            <div" << cls << " dangerouslySetInnerHTML={{ __html: `" << elem->label << "` }} />\n";
            }
        }
        
        file << "            {result && (\n";
        file << "                <div>\n";
        file << "                    <div>API Response</div>\n";
        file << "                    <pre>{JSON.stringify(result, null, 2)}</pre>\n";
        file << "                </div>\n";
        file << "            )}\n";
    } else {
        file << "        <div className=\"min-h-screen bg-[var(--bg-color)] text-[var(--text-color)] flex flex-col justify-center items-center relative overflow-hidden font-sans\">\n";
        file << "            <div className=\"absolute w-[300px] h-[300px] bg-gradient-to-r from-[var(--primary-glow)] to-transparent rounded-full top-[10%] left-[15%] opacity-15 blur-[80px]\" />\n";
        file << "            <div className=\"absolute w-[350px] h-[350px] bg-gradient-to-r from-[var(--secondary-glow)] to-transparent rounded-full bottom-[15%] right-[15%] opacity-15 blur-[80px]\" />\n";
        file << "            \n";
        file << "            <main className=\"w-full max-w-[550px] p-8 z-10\">\n";
        file << "                <section className=\"bg-[var(--card-bg)] backdrop-blur-[20px] border border-[var(--border-color)] rounded-[24px] p-10 shadow-[0_20px_50px_rgba(0,0,0,0.3)]\">\n";
        file << "                    <div className=\"text-center mb-8\">\n";
        
        std::string mainTitle = view->name;
        std::string subTitle = "Hexagen Compiled UI";
        for (const auto& elem : view->elements) {
            if (elem->type == "title") {
                subTitle = elem->label;
            }
        }
        file << "                        <h1 className=\"text-3xl font-extrabold bg-gradient-to-r from-white to-[var(--primary-glow)] bg-clip-text text-transparent mb-2\">" << mainTitle << "</h1>\n";
        file << "                        <p className=\"text-sm text-gray-400\">" << subTitle << "</p>\n";
        file << "                    </div>\n";
        
        file << "                    <div className=\"space-y-6\">\n";

        for (const auto& elem : view->elements) {
            if (elem->type == "input") {
                std::string cls = elem->className.empty() ? "" : " " + elem->className;
                file << "                        <div>\n";
                file << "                            <label className=\"block text-xs font-semibold uppercase text-gray-400 mb-2\">" << elem->name << "</label>\n";
                file << "                            <input type=\"text\" value={" << elem->name << "} onChange={(e) => set" << elem->name << "(e.target.value)} className=\"w-full bg-white/5 border border-[var(--border-color)] rounded-xl px-4 py-3 text-white focus:outline-none focus:border-[var(--primary-glow)] focus:bg-white/10 transition" << cls << "\" />\n";
                file << "                        </div>\n";
            } else if (elem->type == "button") {
                size_t dotPos = elem->targetAction.find('.');
                std::string cls = elem->className.empty() ? "" : " " + elem->className;
                if (dotPos != std::string::npos) {
                    std::string sliceName = elem->targetAction.substr(0, dotPos);
                    std::string actionName = elem->targetAction.substr(dotPos + 1);
                    file << "                        <button onClick={handle" << sliceName << "_" << actionName << "} className=\"w-full bg-gradient-to-r from-[var(--secondary-glow)] to-[var(--primary-glow)] text-[var(--bg-color)] py-4 rounded-xl font-bold hover:scale-[1.02] transition active:scale-[0.98]" << cls << "\">" << elem->label << "</button>\n";
                } else {
                    std::string pathLower = elem->targetAction;
                    std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);
                    file << "                        <button onClick={() => navigate('/" << pathLower << "')} className=\"w-full bg-white/5 border border-[var(--border-color)] py-4 rounded-xl font-bold hover:bg-white/10 transition" << cls << "\">" << elem->label << "</button>\n";
                }
            } else if (elem->type == "html") {
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "                        <div" << cls << " dangerouslySetInnerHTML={{ __html: `" << elem->label << "` }} />\n";
            }
        }
        
        file << "                    </div>\n";

        file << "                    {result && (\n";
        file << "                        <div className=\"mt-8 bg-black/25 rounded-xl border border-white/5 p-5\">\n";
        file << "                            <div className=\"text-xs font-semibold text-[var(--primary-glow)] mb-2 uppercase\">API Response</div>\n";
        file << "                            <pre className=\"font-mono text-xs text-[#e5e7eb] overflow-x-auto\">{JSON.stringify(result, null, 2)}</pre>\n";
        file << "                        </div>\n";
        file << "                    )}\n";
    }

    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            bool hasDeleteRoute = false;
            if (!program->apis.empty()) {
                for (const auto& r : program->allRoutes()) {
                    if (r->method == "DELETE") {
                        size_t dotPos = r->targetAction.find('.');
                        std::string targetSlice = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                        if (targetSlice == elem->label) {
                            hasDeleteRoute = true;
                            break;
                        }
                    }
                }
            }

            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "                    <div" << cls << ">\n";
                file << "                        <table>\n";
                file << "                            <thead>\n";
                file << "                                <tr>\n";
                for (const auto& col : elem->columns) {
                    file << "                                    <th>" << col << "</th>\n";
                }
                if (hasDeleteRoute) {
                    file << "                                    <th>Acciones</th>\n";
                }
                file << "                                </tr>\n";
                file << "                            </thead>\n";
                file << "                            <tbody>\n";
                file << "                                {" << elem->label << "Rows.map((row: any, idx: number) => (\n";
                file << "                                    <tr key={idx}>\n";
                for (const auto& col : elem->columns) {
                    file << "                                        <td>{row." << col << "}</td>\n";
                }
                if (hasDeleteRoute) {
                    std::string keyCol = elem->columns.empty() ? "id" : elem->columns[0];
                    file << "                                        <td>\n";
                    file << "                                            <button onClick={() => handleDelete_" << elem->label << "(row." << keyCol << ")}>Eliminar</button>\n";
                    file << "                                        </td>\n";
                }
                file << "                                    </tr>\n";
                file << "                                ))}\n";
                file << "                            </tbody>\n";
                file << "                        </table>\n";
                file << "                    </div>\n";
            } else {
                std::string cls = elem->className.empty() ? "" : " " + elem->className;
                file << "                    <div className=\"mt-8 bg-black/20 rounded-xl border border-white/10 overflow-hidden" << cls << "\">\n";
                file << "                        <table className=\"w-full text-left border-collapse\">\n";
                file << "                            <thead>\n";
                file << "                                <tr className=\"bg-white/5 text-xs font-semibold text-gray-400 uppercase\">\n";
                for (const auto& col : elem->columns) {
                    file << "                                    <th className=\"p-3\">" << col << "</th>\n";
                }
                if (hasDeleteRoute) {
                    file << "                                    <th className=\"p-3\">Acciones</th>\n";
                }
                file << "                                </tr>\n";
                file << "                            </thead>\n";
                file << "                            <tbody>\n";
                file << "                                {" << elem->label << "Rows.map((row: any, idx: number) => (\n";
                file << "                                    <tr key={idx} className=\"border-b border-white/10\">\n";
                for (const auto& col : elem->columns) {
                    file << "                                        <td className=\"p-3 text-sm\">{row." << col << "}</td>\n";
                }
                if (hasDeleteRoute) {
                    std::string keyCol = elem->columns.empty() ? "id" : elem->columns[0];
                    file << "                                        <td className=\"p-3 text-sm\">\n";
                    file << "                                            <button onClick={() => handleDelete_" << elem->label << "(row." << keyCol << ")} className=\"px-3 py-1 text-xs font-semibold rounded bg-gradient-to-r from-red-500 to-rose-600 text-white hover:scale-105 active:scale-95 transition\">Eliminar</button>\n";
                    file << "                                        </td>\n";
                }
                file << "                                    </tr>\n";
                file << "                                ))}\n";
                file << "                            </tbody>\n";
                file << "                        </table>\n";
                file << "                    </div>\n";
            }
        }
    }

    if (program->css == "none") {
        file << "        </>\n";
    } else {
        file << "                </section>\n";
        file << "            </main>\n";
        file << "        </div>\n";
    }
    file << "    );\n";
    file << "}\n";
}

std::string CodeGenerator::generateAdminHTML() {
    std::stringstream ss;
    ss << R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Hexagen Admin Portal</title>
<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">
<style>
body {
    margin: 0;
    font-family: 'Outfit', sans-serif;
    background: #0b0f19;
    color: #f3f4f6;
    display: flex;
    height: 100vh;
}
sidebar {
    width: 260px;
    background: #111827;
    border-right: 1px solid #1f2937;
    padding: 24px;
    display: flex;
    flex-direction: column;
}
sidebar h2 {
    font-size: 20px;
    margin: 0 0 24px 0;
    background: linear-gradient(135deg, #00f2fe 0%, #4facfe 100%);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    font-weight: 800;
}
.slice-btn {
    padding: 12px 16px;
    border-radius: 12px;
    cursor: pointer;
    margin-bottom: 8px;
    transition: all 0.2s;
    background: transparent;
    border: none;
    color: #9ca3af;
    text-align: left;
    font-size: 16px;
    width: 100%;
}
.slice-btn:hover, .slice-btn.active {
    background: rgba(0, 242, 254, 0.1);
    color: #00f2fe;
}
content {
    flex: 1;
    padding: 40px;
    overflow-y: auto;
}
.card {
    background: rgba(17, 24, 39, 0.7);
    backdrop-filter: blur(10px);
    border: 1px solid rgba(255,255,255,0.05);
    border-radius: 24px;
    padding: 32px;
    box-shadow: 0 20px 40px rgba(0,0,0,0.3);
}
table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 24px;
}
th, td {
    padding: 16px;
    text-align: left;
    border-bottom: 1px solid #1f2937;
}
th {
    color: #9ca3af;
    font-weight: 600;
}
.btn {
    background: linear-gradient(135deg, #00f2fe 0%, #4facfe 100%);
    border: none;
    color: white;
    padding: 10px 20px;
    border-radius: 12px;
    cursor: pointer;
    font-weight: 600;
    transition: transform 0.2s;
}
.btn:hover {
    transform: scale(1.03);
}
.btn-danger {
    background: linear-gradient(135deg, #f87171 0%, #ef4444 100%);
}
.modal {
    display: none;
    position: fixed;
    top: 0; left: 0; width: 100%; height: 100%;
    background: rgba(0,0,0,0.6);
    justify-content: center; align-items: center;
    backdrop-filter: blur(5px);
}
.modal-content {
    background: #111827;
    border: 1px solid #1f2937;
    padding: 32px;
    border-radius: 24px;
    width: 400px;
}
.input-group {
    margin-bottom: 16px;
}
.input-group label {
    display: block; margin-bottom: 8px; color: #9ca3af;
}
.input-group input {
    width: 100%; padding: 10px; border-radius: 8px; border: 1px solid #1f2937; background: #1f2937; color: white; box-sizing: border-box;
}
</style>
</head>
<body>
<sidebar>
    <h2>Hexagen Admin</h2>
    <div id="slice-list"></div>
</sidebar>
<content>
    <div class="card" id="main-card">
        <h1 id="slice-title">Welcome to Admin Portal</h1>
        <p>Select a slice from the sidebar to manage database records.</p>
    </div>
</content>

<div class="modal" id="add-modal">
    <div class="modal-content">
        <h3 style="margin-top:0;">Add New Record</h3>
        <form id="add-form"></form>
        <div style="display:flex; justify-content: flex-end; gap: 12px; margin-top: 24px;">
            <button class="btn btn-danger" onclick="closeModal()">Cancel</button>
            <button class="btn" onclick="submitRecord()">Save</button>
        </div>
    </div>
</div>

<script>
)HTML";

    ss << "const slices = {\n";
    for (const auto& slice : program->slices) {
        ss << "    \"" << slice->name << "\": [\n";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            ss << "        { \"name\": \"" << slice->fields[i]->name << "\", \"type\": \"" << dataTypeToString(slice->fields[i]->type) << "\" }";
            if (i + 1 < slice->fields.size()) ss << ",";
            ss << "\n";
        }
        ss << "    ],\n";
    }
    ss << "};\n";

    ss << R"HTML(
let activeSlice = '';

function renderSidebar() {
    const list = document.getElementById('slice-list');
    list.innerHTML = '';
    Object.keys(slices).forEach(s => {
        const btn = document.createElement('button');
        btn.className = 'slice-btn';
        btn.innerText = s;
        btn.onclick = () => selectSlice(s);
        list.appendChild(btn);
    });
}

async function selectSlice(name) {
    activeSlice = name;
    document.querySelectorAll('.slice-btn').forEach(btn => {
        btn.classList.toggle('active', btn.innerText === name);
    });
    
    const fields = slices[name];
    const card = document.getElementById('main-card');
    card.innerHTML = `
        <div style="display:flex; justify-content:space-between; align-items:center;">
            <h1 style="margin:0;">${name}</h1>
            <button class="btn" onclick="openAddModal()">Add Record</button>
        </div>
        <div style="overflow-x:auto;">
            <table>
                <thead>
                    <tr>
                        ${fields.map(f => `<th>${f.name}</th>`).join('')}
                        <th>Actions</th>
                    </tr>
                </thead>
                <tbody id="table-body"></tbody>
            </table>
        </div>
    `;
    loadTableData();
}

async function loadTableData() {
    const res = await fetch('/api/admin/' + activeSlice);
    const data = await res.json();
    const tbody = document.getElementById('table-body');
    tbody.innerHTML = '';
    data.forEach(row => {
        const tr = document.createElement('tr');
        const fields = slices[activeSlice];
        tr.innerHTML = fields.map(f => `<td>${row[f.name]}</td>`).join('') + 
            `<td><button class="btn btn-danger" onclick="deleteRecord('${row[fields[0].name]}')">Delete</button></td>`;
        tbody.appendChild(tr);
    });
}

function openAddModal() {
    const form = document.getElementById('add-form');
    form.innerHTML = '';
    slices[activeSlice].forEach(f => {
        form.innerHTML += `
            <div class="input-group">
                <label>${f.name} (${f.type})</label>
                <input type="${f.type === 'int' || f.type === 'float' ? 'number' : 'text'}" name="${f.name}" required>
            </div>
        `;
    });
    document.getElementById('add-modal').style.display = 'flex';
}

function closeModal() {
    document.getElementById('add-modal').style.display = 'none';
}

async function submitRecord() {
    const form = document.getElementById('add-form');
    const data = {};
    slices[activeSlice].forEach(f => {
        const val = form.elements[f.name].value;
        data[f.name] = f.type === 'int' ? parseInt(val) : f.type === 'float' ? parseFloat(val) : val;
    });
    await fetch('/api/admin/' + activeSlice, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    });
    closeModal();
    loadTableData();
}

async function deleteRecord(id) {
    if(!confirm('Are you sure you want to delete this record?')) return;
    const fields = slices[activeSlice];
    const payload = {};
    payload[fields[0].name] = id;
    await fetch('/api/admin/' + activeSlice, {
        method: 'DELETE',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });
    loadTableData();
}

renderSidebar();
</script>
</body>
</html>
)HTML";

    return ss.str();
}

