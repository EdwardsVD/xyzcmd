({
  name: 'pyodide',
  version: '1.0.0',
  description: 'Python 3.12 REPL via Pyodide WASM',

  onInstall: function(ctx) {
    ctx.writeLine('[PKG] pyodide v1.0.0 installed', 'success');
    ctx.writeLine('[INFO] Run: xyz run pyodide  to start Python REPL', 'info');
    ctx.writeLine('[INFO] Pyodide loads ~10MB on first run (cached after)', 'info');
  },

  commands: {
    pyodide: {
      description: 'Python 3.12 REPL via Pyodide WASM',
      run: async function(args, ctx) {

        // Load Pyodide script dari CDN jika belum ada
        if (!window.loadPyodide) {
          ctx.writeLine('[PKG] Downloading Pyodide runtime (~10MB)...', 'info');
          ctx.writeLine('[INFO] This only happens once. Please wait.', 'info');
          await new Promise(function(resolve, reject) {
            var s = document.createElement('script');
            s.src = 'https://cdn.jsdelivr.net/pyodide/v0.26.4/full/pyodide.js';
            s.onload = resolve;
            s.onerror = function() { reject(new Error('Failed to load Pyodide CDN')); };
            document.head.appendChild(s);
          });
        }

        // Init Pyodide instance (cache ke window)
        if (!window._pyodideInstance) {
          try {
            ctx.writeLine('[PKG] Initializing Python 3.12...', 'info');
            window._pyodideInstance = await window.loadPyodide({
              indexURL: 'https://cdn.jsdelivr.net/pyodide/v0.26.4/full/',
              stdout: function(t) { ctx.writeLine(t, 'python'); },
              stderr: function(t) { ctx.writeLine(t, 'error'); },
            });
            ctx.writeLine('[OK] Python 3.12 ready', 'success');
          } catch(e) {
            ctx.writeLine('[ERR] Failed to initialize Pyodide: ' + e.message, 'error');
            return;
          }
        } else {
          ctx.writeLine('[OK] Python 3.12 ready (cached)', 'success');
        }

        var py = window._pyodideInstance;

        // Banner
        ctx.writeLine('', 'normal');
        ctx.writeLine('Python 3.12 | Pyodide v0.26.4 | pyodide plugin v1.0.0', 'info');
        ctx.writeLine('Type Python code line by line.', 'info');
        ctx.writeLine('Commands: exit() | quit() | xyz exit pyodide', 'info');
        ctx.writeLine('Packages: import micropip; await micropip.install("requests")', 'info');
        ctx.writeLine('', 'normal');

        var buffer = [];
        var inBlock = false;

        while (true) {
          var ps = inBlock ? '... ' : '>>> ';
          var line;
          try {
            line = await ctx.prompt(ps);
          } catch(e) {
            ctx.writeLine('\n[INFO] KeyboardInterrupt', 'info');
            break;
          }

          // Exit commands
          var trimmed = line.trim();
          if (
            trimmed === 'exit()' ||
            trimmed === 'quit()' ||
            trimmed === 'xyz exit pyodide' ||
            trimmed === 'exit' ||
            trimmed === 'quit'
          ) {
            ctx.writeLine('[OK] Exiting Python REPL', 'info');
            break;
          }

          // Empty line — flush buffer jika dalam block
          if (trimmed === '') {
            if (inBlock && buffer.length > 0) {
              inBlock = false;
              var code = buffer.join('\n');
              buffer = [];
              try {
                var result = await py.runPythonAsync(code);
                if (result !== undefined && result !== null) {
                  ctx.writeLine(String(result), 'python');
                }
              } catch(e) {
                ctx.writeLine(e.message, 'error');
              }
            }
            continue;
          }

          // Detect block start
          var blockStarters = /^(if|for|while|def|class|with|try|else|elif|except|finally|async\s+def|async\s+for|async\s+with)\b/;
          if (blockStarters.test(trimmed) || trimmed.endsWith(':') || inBlock) {
            buffer.push(line);
            inBlock = true;
            continue;
          }

          // Single line — run langsung
          try {
            var result = await py.runPythonAsync(line);
            if (result !== undefined && result !== null) {
              ctx.writeLine(String(result), 'python');
            }
          } catch(e) {
            ctx.writeLine(e.message, 'error');
          }
        }
      }
    }
  }
})
