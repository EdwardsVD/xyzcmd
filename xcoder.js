({
  name: 'xcoder',
  version: '1.0.0',
  description: 'AI coding assistant powered by Gemini/OpenRouter',
  author: 'xyzcmd',

  commands: {
    xcoder: {
      description: 'AI coding assistant with file download support',
      run: async function(args, ctx) {

        // ========== BANNER ==========
        var BANNER = [
          '',
          '██╗  ██╗ ██████╗ ██████╗ ██████╗ ███████╗██████╗ ',
          '╚██╗██╔╝██╔════╝██╔═══██╗██╔══██╗██╔════╝██╔══██╗',
          ' ╚███╔╝ ██║     ██║   ██║██║  ██║█████╗  ██████╔╝',
          ' ██╔██╗ ██║     ██║   ██║██║  ██║██╔══╝  ██╔══██╗',
          '██╔╝ ██╗╚██████╗╚██████╔╝██████╔╝███████╗██║  ██║',
          '╚═╝  ╚═╝ ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝╚═╝  ╚═╝',
          'AI Coding Assistant v1.0.0',
          ''
        ];

        // ========== PROVIDERS ==========
        var PROVIDERS = {
          gemini: {
            name: 'Google Gemini',
            endpoint: 'https://generativelanguage.googleapis.com/v1beta/openai/chat/completions',
            models: [
              'gemini-2.0-flash',
              'gemini-2.5-pro',
              'gemini-1.5-flash',
              'gemini-1.5-pro'
            ],
            defaultModel: 'gemini-2.0-flash'
          },
          openrouter: {
            name: 'OpenRouter',
            endpoint: 'https://openrouter.ai/api/v1/chat/completions',
            models: [
              'google/gemini-2.0-flash-exp:free',
              'google/gemini-2.5-pro-exp-03-25:free',
              'deepseek/deepseek-chat-v3-0324:free',
              'meta-llama/llama-3.3-70b-instruct:free'
            ],
            defaultModel: 'google/gemini-2.0-flash-exp:free'
          }
        };

        // ========== SYSTEM PROMPT ==========
        var SYSTEM_PROMPT = 'You are XCODER, an expert AI coding assistant running inside a browser terminal.\n\n' +
          'THINKING PROCESS (always follow this internally):\n' +
          '1. UNDERSTAND: What exactly is the user asking? What is the goal?\n' +
          '2. PLAN: Break the solution into clear steps before writing code.\n' +
          '3. EXECUTE: Write clean, complete, production-ready code.\n' +
          '4. VERIFY: Check for bugs, edge cases, and improvements.\n' +
          '5. EXPLAIN: Briefly explain what the code does and how to use it.\n\n' +
          'CODING RULES:\n' +
          '- Always write complete, runnable code. Never write partial snippets.\n' +
          '- Include error handling in every function.\n' +
          '- Add comments for complex logic.\n' +
          '- Use modern syntax (ES2022+, Python 3.10+).\n' +
          '- If multiple files are needed, clearly label each file with its filename.\n\n' +
          'OUTPUT FORMAT FOR CODE:\n' +
          '- Always wrap code in proper markdown code blocks with language tag.\n' +
          '- If generating multiple files, label each clearly:\n' +
          '  === filename.py ===\n' +
          '  (code here)\n' +
          '  === index.html ===\n' +
          '  (code here)\n' +
          '- After code blocks, add: [DOWNLOAD: filename.ext] or [DOWNLOAD: project.zip]\n\n' +
          'DOWNLOAD INSTRUCTION:\n' +
          'When you generate code files, ALWAYS end your response with:\n' +
          '- Single file: [DOWNLOAD: exact-filename.ext]\n' +
          '- Multiple files: [DOWNLOAD: project-name.zip]\n' +
          'This signals the terminal to show a download button.\n\n' +
          'If asked in Indonesian, reply in Indonesian.';

        // ========== STORAGE FOR GENERATED FILES ==========
        var lastGeneratedFiles = {};

        // ========== HELPERS ==========
        function wLine(text, type) {
          if (ctx.writeLine) {
            ctx.writeLine(text, type || 'normal');
          } else if (ctx.print) {
            ctx.print(text);
          } else {
            console.log(text);
          }
        }

        function wChar(ch) {
          if (ctx.writeChar) {
            ctx.writeChar(ch);
          }
        }

        async function readInput(promptText) {
          if (ctx.readLine) return await ctx.readLine(promptText);
          if (ctx.prompt) return await ctx.prompt(promptText);
          return '';
        }

        function loadConfig() {
          try {
            var raw = localStorage.getItem('xcoder_config');
            if (raw) return JSON.parse(raw);
          } catch(e) {}
          return null;
        }

        function saveConfig(cfg) {
          localStorage.setItem('xcoder_config', JSON.stringify(cfg));
        }

        function loadHistory() {
          try {
            var raw = localStorage.getItem('xcoder_history');
            if (raw) return JSON.parse(raw);
          } catch(e) {}
          return [];
        }

        function saveHistory(hist) {
          var trimmed = hist.slice(-20);
          localStorage.setItem('xcoder_history', JSON.stringify(trimmed));
        }

        function clearHistory() {
          localStorage.removeItem('xcoder_history');
        }

        function getNextKey(keys) {
          var idx = parseInt(localStorage.getItem('xcoder_key_idx') || '0', 10);
          if (idx >= keys.length) idx = 0;
          var key = keys[idx];
          var next = (idx + 1) % keys.length;
          localStorage.setItem('xcoder_key_idx', String(next));
          return key;
        }

        // ========== JSZip LOADER ==========
        async function loadJSZip() {
          if (typeof JSZip !== 'undefined') return;
          var script = document.createElement('script');
          script.src = 'https://cdn.jsdelivr.net/npm/jszip@3.10.1/dist/jszip.min.js';
          document.head.appendChild(script);
          await new Promise(function(resolve) { script.onload = resolve; });
        }

        // ========== DOWNLOAD FUNCTIONS ==========
        function downloadFile(filename, content) {
          var blob = new Blob([content], { type: 'text/plain' });
          var a = document.createElement('a');
          a.href = URL.createObjectURL(blob);
          a.download = filename;
          document.body.appendChild(a);
          a.click();
          document.body.removeChild(a);
          URL.revokeObjectURL(a.href);
        }

        async function downloadZip(zipName, files) {
          await loadJSZip();
          var zip = new JSZip();
          files.forEach(function(f) {
            zip.file(f.name, f.content);
          });
          var blob = await zip.generateAsync({ type: 'blob' });
          var a = document.createElement('a');
          a.href = URL.createObjectURL(blob);
          a.download = zipName;
          document.body.appendChild(a);
          a.click();
          document.body.removeChild(a);
          URL.revokeObjectURL(a.href);
        }

        // ========== PARSE CODE BLOCKS ==========
        function parseCodeBlocks(text) {
          var files = [];
          
          // Pattern for === filename.ext ===
          var labelPattern = /===\s*([^\s=]+\.[a-z0-9]+)\s*===/gi;
          var codeBlockPattern = /```[\w]*\n([\s\S]*?)```/g;
          
          var codeBlocks = [];
          var match;
          while ((match = codeBlockPattern.exec(text)) !== null) {
            codeBlocks.push(match[1]);
          }
          
          // Find labels before code blocks
          var labels = [];
          while ((match = labelPattern.exec(text)) !== null) {
            labels.push({ name: match[1], index: match.index });
          }
          
          if (labels.length > 0 && labels.length === codeBlocks.length) {
            for (var i = 0; i < labels.length; i++) {
              files.push({ name: labels[i].name, content: codeBlocks[i].trim() });
            }
          } else if (codeBlocks.length === 1) {
            // Single code block - use download pattern filename
            var dlMatch = /\[DOWNLOAD:\s*([^\]]+)\]/.exec(text);
            var filename = dlMatch ? dlMatch[1].trim() : 'code.txt';
            files.push({ name: filename, content: codeBlocks[0].trim() });
          } else if (codeBlocks.length > 1) {
            // Multiple blocks without labels - name them generically
            codeBlocks.forEach(function(code, idx) {
              files.push({ name: 'file' + (idx + 1) + '.txt', content: code.trim() });
            });
          }
          
          return files;
        }

        // ========== PROCESS AI RESPONSE FOR DOWNLOADS ==========
        function processResponseForDownloads(responseText) {
          var files = parseCodeBlocks(responseText);
          
          // Store files for later download
          files.forEach(function(f) {
            lastGeneratedFiles[f.name] = f;
          });
          
          // Check for download pattern
          var dlMatch = /\[DOWNLOAD:\s*([^\]]+)\]/.exec(responseText);
          if (dlMatch) {
            var filename = dlMatch[1].trim();
            
            if (filename.endsWith('.zip') && files.length > 1) {
              // Will be handled by download button click
              wLine('');
              wLine('[DL] ' + files.length + ' files ready. Click Download or type: xcoder download ' + filename, 'success');
            } else if (files.length > 0) {
              wLine('');
              wLine('[DL] File ready. Click Download or type: xcoder download ' + filename, 'success');
            }
          }
          
          return files;
        }

        // ========== API CALL WITH STREAMING ==========
        async function callAI(messages, config, onChunk) {
          var key = getNextKey(config.keys);
          var resp = await fetch(config.endpoint, {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json',
              'Authorization': 'Bearer ' + key
            },
            body: JSON.stringify({
              model: config.model,
              messages: messages,
              temperature: config.temperature || 0.7,
              stream: true
            })
          });

          if (resp.status === 429) {
            throw { code: 429, message: 'Rate limit, trying next key...' };
          }

          if (!resp.ok) {
            var errText = await resp.text();
            throw new Error('API Error ' + resp.status + ': ' + errText);
          }

          var reader = resp.body.getReader();
          var decoder = new TextDecoder();
          var fullText = '';

          while (true) {
            var result = await reader.read();
            if (result.done) break;
            var chunk = decoder.decode(result.value, { stream: true });
            var lines = chunk.split('\n').filter(function(l) { return l.startsWith('data: '); });
            for (var i = 0; i < lines.length; i++) {
              var data = lines[i].replace('data: ', '').trim();
              if (data === '[DONE]') return fullText;
              try {
                var parsed = JSON.parse(data);
                var text = '';
                if (parsed.choices && parsed.choices[0] && parsed.choices[0].delta && parsed.choices[0].delta.content) {
                  text = parsed.choices[0].delta.content;
                }
                if (text) {
                  fullText += text;
                  onChunk(text);
                }
              } catch(e) {}
            }
          }
          return fullText;
        }

        async function callAIWithRetry(messages, config, onChunk) {
          var maxRetries = config.keys.length;
          for (var attempt = 0; attempt < maxRetries; attempt++) {
            try {
              return await callAI(messages, config, onChunk);
            } catch(err) {
              if (err && err.code === 429 && attempt < maxRetries - 1) {
                wLine('[WARN] Rate limit hit. Rotating key... (' + (attempt + 1) + '/' + maxRetries + ')', 'warning');
                continue;
              }
              throw err;
            }
          }
          throw new Error('All API keys exhausted (rate limited).');
        }

        // ========== SETUP FLOW ==========
        async function runSetup() {
          wLine('');
          wLine('[SETUP] Welcome to XCODER!', 'info');
          wLine('[SETUP] Let\'s configure your AI provider.', 'info');
          wLine('');

          // Step 1: Provider
          wLine('Select provider:', 'normal');
          wLine('  1. Google Gemini (free tier available)', 'normal');
          wLine('  2. OpenRouter (multi-model)', 'normal');
          wLine('  3. Custom endpoint', 'normal');
          wLine('');

          var providerChoice = await readInput('Enter choice (1-3): ');
          providerChoice = (providerChoice || '').trim();

          var providerKey = 'gemini';
          var endpoint = '';
          var providerName = '';
          var modelList = [];
          var defaultModel = '';

          if (providerChoice === '2') {
            providerKey = 'openrouter';
          } else if (providerChoice === '3') {
            providerKey = 'custom';
          } else {
            providerKey = 'gemini';
          }

          if (providerKey === 'custom') {
            providerName = 'Custom';
            endpoint = await readInput('Enter API endpoint URL: ');
            endpoint = (endpoint || '').trim();
            modelList = [];
            defaultModel = '';
          } else {
            var prov = PROVIDERS[providerKey];
            providerName = prov.name;
            endpoint = prov.endpoint;
            modelList = prov.models;
            defaultModel = prov.defaultModel;
          }

          // Step 2: API key
          wLine('');
          var apiKey = await readInput('Enter API key: ');
          apiKey = (apiKey || '').trim();

          if (!apiKey) {
            wLine('[ERR] No API key provided. Setup aborted.', 'error');
            return null;
          }

          var keys = apiKey.split(',').map(function(k) { return k.trim(); }).filter(function(k) { return k.length > 0; });

          // Step 3: Model selection
          wLine('');
          if (modelList.length > 0) {
            wLine('Available models:', 'normal');
            for (var m = 0; m < modelList.length; m++) {
              wLine('  ' + (m + 1) + '. ' + modelList[m], 'normal');
            }
            wLine('');
          }

          var modelInput = await readInput('Enter model name (or pick 1-' + modelList.length + '): ');
          modelInput = (modelInput || '').trim();
          var chosenModel = defaultModel;

          if (modelInput) {
            var mIdx = parseInt(modelInput, 10);
            if (!isNaN(mIdx) && mIdx >= 1 && mIdx <= modelList.length) {
              chosenModel = modelList[mIdx - 1];
            } else {
              chosenModel = modelInput;
            }
          }

          if (!chosenModel) {
            chosenModel = defaultModel || 'gemini-2.0-flash';
          }

          // Save config
          var config = {
            provider: providerKey,
            providerName: providerName,
            endpoint: endpoint,
            keys: keys,
            model: chosenModel,
            temperature: 0.7
          };

          saveConfig(config);
          localStorage.removeItem('xcoder_key_idx');
          clearHistory();

          wLine('');
          wLine('[OK] Configuration saved.', 'success');
          wLine('[OK] Type your message below. Type \'exit\' to quit.', 'success');
          wLine('[OK] Type \'xcoder download <filename>\' to download generated files.', 'success');
          wLine('');

          return config;
        }

        // ========== HANDLE RESET COMMAND ==========
        if (args && args[0] === 'reset') {
          localStorage.removeItem('xcoder_config');
          localStorage.removeItem('xcoder_history');
          localStorage.removeItem('xcoder_key_idx');
          wLine('[OK] Configuration cleared. Run xcoder again to set up.', 'success');
          return;
        }

        // ========== HANDLE DOWNLOAD COMMAND ==========
        if (args && args[0] === 'download') {
          var filename = args.slice(1).join(' ');
          if (!filename) {
            wLine('[ERR] Usage: xcoder download <filename>', 'error');
            return;
          }
          
          var file = lastGeneratedFiles[filename];
          if (!file) {
            // Check if it's a zip request
            if (filename.endsWith('.zip')) {
              var allFiles = Object.values(lastGeneratedFiles);
              if (allFiles.length > 0) {
                wLine('[DL] Packing ' + allFiles.length + ' files into ' + filename + '...', 'info');
                await downloadZip(filename, allFiles);
                wLine('[DL] ' + filename + ' downloaded.', 'success');
                return;
              }
            }
            wLine('[ERR] File \'' + filename + '\' not found. Ask AI to generate it first.', 'error');
            return;
          }
          
          wLine('[DL] Downloading ' + filename + '...', 'info');
          downloadFile(filename, file.content);
          wLine('[DL] ' + filename + ' downloaded.', 'success');
          return;
        }

        // ========== SHOW BANNER ==========
        for (var bi = 0; bi < BANNER.length; bi++) {
          wLine(BANNER[bi], 'info');
        }

        // ========== LOAD OR SETUP ==========
        var config = loadConfig();

        if (!config) {
          config = await runSetup();
          if (!config) return;
        } else {
          wLine('[OK] Configuration loaded. Type your message below.', 'success');
          wLine('[OK] Type \'exit\' to quit, \'xcoder reset\' to reconfigure.', 'success');
          wLine('');
        }

        // ========== MAIN CHAT LOOP ==========
        var history = loadHistory();
        var running = true;

        while (running) {
          var input = await readInput('xcoder> ');
          if (input === null || input === undefined) {
            running = false;
            break;
          }

          input = input.trim();
          if (!input) continue;

          // Handle exit
          if (input.toLowerCase() === 'exit' || input.toLowerCase() === 'quit') {
            wLine('[OK] Goodbye!', 'info');
            running = false;
            break;
          }

          // Handle clear
          if (input.toLowerCase() === 'clear') {
            history = [];
            clearHistory();
            lastGeneratedFiles = {};
            wLine('[OK] Conversation cleared.', 'success');
            continue;
          }

          // Handle help
          if (input.toLowerCase() === 'help') {
            wLine('');
            wLine('XCODER Commands:', 'info');
            wLine('  exit, quit     Exit xcoder', 'normal');
            wLine('  clear          Clear conversation history', 'normal');
            wLine('  help           Show this help', 'normal');
            wLine('');
            wLine('After AI generates code, click [Download] button or type:', 'info');
            wLine('  xcoder download <filename>', 'normal');
            wLine('');
            continue;
          }

          // Send message to AI
          var messages = [{ role: 'system', content: SYSTEM_PROMPT }];

          for (var hi = 0; hi < history.length; hi++) {
            messages.push(history[hi]);
          }
          messages.push({ role: 'user', content: input });

          // Response header
          wLine('');
          wLine('[XCODER | ' + config.model + ']', 'info');
          wLine('----------------------------------------', 'normal');

          var startTime = Date.now();
          var fullResponse = '';

          try {
            fullResponse = await callAIWithRetry(messages, config, function(chunk) {
              wChar(chunk);
            });
          } catch(err) {
            wLine('');
            var errMsg = (err && err.message) ? err.message : String(err);
            wLine('[ERR] ' + errMsg, 'error');
            continue;
          }

          var elapsed = ((Date.now() - startTime) / 1000).toFixed(1);

          wLine('');
          wLine('----------------------------------------', 'normal');
          wLine('[' + elapsed + 's]', 'normal');

          // Process for downloads
          processResponseForDownloads(fullResponse);

          // Save to history
          history.push({ role: 'user', content: input });
          history.push({ role: 'assistant', content: fullResponse });

          if (history.length > 20) {
            history = history.slice(history.length - 20);
          }
          saveHistory(history);
          
          wLine('');
        }
      }
    },

    download: {
      description: 'Download a file generated by AI',
      run: async function(args, ctx) {
        // This is handled by the main xcoder command with 'download' arg
        // But we expose it as a separate command too for convenience
        var filename = args.join(' ');
        if (!filename) {
          ctx.writeLine('[ERR] Usage: xcoder download <filename>', 'error');
          ctx.writeLine('       or: download <filename>', 'error');
          return;
        }
        ctx.writeLine('[INFO] Use "xcoder download ' + filename + '" inside xcoder chat.', 'info');
      }
    }
  },

  onInstall: function(ctx) {
    function wLine(text, type) {
      if (ctx.writeLine) {
        ctx.writeLine(text, type || 'normal');
      } else if (ctx.print) {
        ctx.print(text);
      } else {
        console.log(text);
      }
    }

    wLine('');
    wLine('+====================================+', 'info');
    wLine('|    XCODER v1.0.0 INSTALLED         |', 'info');
    wLine('|    AI Coding Assistant for XYZ     |', 'info');
    wLine('+====================================+', 'info');
    wLine('|  Type: xcoder                      |', 'normal');
    wLine('|  Reset: xcoder reset               |', 'normal');
    wLine('|  Download: xcoder download <file>  |', 'normal');
    wLine('+====================================+', 'info');
    wLine('');
    wLine('Type \'xcoder\' to start.', 'success');
    wLine('');
  }
})
