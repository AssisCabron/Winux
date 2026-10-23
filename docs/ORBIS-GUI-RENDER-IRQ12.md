# ORBIS OS — GUI nativa, tipografia proporcional e mouse IRQ12

Data: 23/10/2026. Estado: **preview experimental QEMU/UEFI**, não promovido à ISO Winux 0.38 estável.

## Correção da interface

O código de interface é executado diretamente no framebuffer UEFI GOP do kernel em `src/ui/orbis_gui.h`. Não há navegador, Electron ou HTML.

- **Sistema tipográfico Orbis Sans (conceito):** `scripts/build/generate_orbis_font.ps1` rasteriza ASCII 32–126 da Segoe UI instalada no Windows em **dois atlases 8 bpp**, regular para textos e negrito maior para títulos. Há avanço proporcional individual por caractere e antialiasing de 256 níveis. `src/ui/orbis_design.h` implementa desenho de títulos, marca orbital, cards, campos, botões e a paleta do conceito (`#FF963F` laranja, `#267AF5` azul, superfícies grafite). **Não existe um arquivo de fonte vetorial Orbis Sans nem um framework Qt completo**: a fonte é uma substituição rasterizada local, sem distribuir os arquivos de fonte. A imagem especimen gerada é uma referência estética, não fornece curvas tipográficas utilizáveis.
- **Wallpaper:** `scripts/build/generate_orbis_rgb888.ps1` prepara uma fotografia panorâmica a 1280×800 RGB888, sem RGB565 ou desfoque de ampliação na resolução QEMU utilizada. O original em `src/ui/assets/orbis-wallpaper-golden.jpg` é uma fotografia temporária de referência, não exatamente a arte gerada anteriormente no ChatGPT. O recurso de 3.072.000 bytes é embutido via assembler `.incbin` e objeto COFF; a ISO estável não é alterada. Para usar um wallpaper diferente, substitua legalmente a fonte, preserve a proporção e recompile.
- **Telas 4.1:** splash, configuração inicial e login usam uma escala tipográfica mais leve (15 px para leitura e 28 px para títulos), cartões com vidro fosco calculado em software, separação ambiente plana, cantos arredondados, marca orbital antialias com traço mais espesso, ícones nativos para o resumo da configuração, campos de usuário/PIN e botão com gradiente suave. A hierarquia do setup foi simplificada removendo o subtítulo redundante sob o título principal. O wallpaper recebe vinheta e tratamento de luz durante a composição. A composição lateral e os espaçamentos seguem o conceito enviado em 23/10/2026. São desenhados diretamente no framebuffer; não são imagens de protótipo exibidas como fundo.
- **Ícones gerados:** os símbolos do setup, campos, botão, menu de aplicativos, dock, atalho Home e pastas não são mais construídos com primitivas geométricas do kernel. Dois atlas raster gerados estão preservados em `src/ui/assets/generated/`. `scripts/build/generate_orbis_icons.ps1` recorta, normaliza, remove resíduos transparentes e produz um atlas BGRA de 32 células. O build incorpora esse recurso na EFI e `or_asset_icon` apenas compõe seus pixels no framebuffer. O hash do atlas entra no fonte assembler temporário para invalidar corretamente o cache do compilador quando a arte muda.
- **Digitação sem redraw completo:** alterações de nome e PIN marcam `or_entry_dirty` e repintam somente a superfície opaca do campo ativo, seu ícone e o texto ou máscara. Transições de tela e mensagens de erro continuam usando o redraw completo. Isso evita reconstruir wallpaper, vidro fosco e todos os painéis a cada tecla.
- **Painéis:** composição com alpha em software e contornos arredondados antialias de um pixel. O contorno usa o mesmo raio de 17 px do preenchimento, eliminando o pequeno gancho quadrado nos cantos. O compositor geral de janelas, vetores SVG, animações, GPU e fontes internacionais completas ainda não estão implementados.

## Mouse responsivo

A implementação anterior lia PS/2 apenas no loop de polling e até desenhava o cursor dentro de `CLI`. Isso causava latência e prejudicava interrupções durante operações gráficas.

Agora `src/arch/x86_64/hardware.h` instala gate de IRQ12 (vetor 44) **somente com WINUX_ORBIS_GUI_PREVIEW**, recebe o byte auxiliar do i8042 e o coloca em uma fila circular de 128 bytes, fazendo EOI no PIC escravo e mestre. O frame da interrupção salva/restaura registradores usados. Não são feitas pinturas na ISR.

Em `src/ui/orbis_gui.h`, a inicialização habilita IRQ12 no controlador i8042 e nos dois PICs, solicita a taxa PS/2 padrão máxima de 200 Hz e o loop lê a fila, reabilita interrupções e só então atualiza o cursor. A fila é drenada sem pausas entre os três bytes de cada pacote. Antes de `HLT`, ela é verificada novamente com interrupções mascaradas e a sequência `STI; HLT` fecha a janela de perda de wakeup que podia acrescentar esperas do PIT de 10 ms. O ponteiro usa uma máscara explícita de 16×22 pixels, com preenchimento branco e contorno escuro contínuo; isso substitui a antiga fórmula geométrica, que produzia segmentos sobrepostos e uma haste irregular. Movimentos normais salvam/restauram somente os 16×22 pixels sob o cursor. Clique pode causar redraw completo, mas nenhum redraw é feito em contexto de IRQ.

Limites: single CPU, PS/2 legado, sem USB HID, sem aceleração, sem compositor com dirty rectangles completo, sem medições de latência no computador físico.

## Testes

```powershell
python scripts/build/make_proc_space_preview.py --experiment integration --orbis-gui
python tests/runners/test_orbis_gui_qemu.py
python scripts/build/run_orbis_gui_visible.py
# ou, para manter uma janela comum do QEMU:
python scripts/build/run_orbis_gui_visible.py --windowed
```

O teste automatizado usa QEMU separado em `-display none`, injeta scancodes pelo PS/2 real, valida boot splash, configuração local de RAM, rejeição de PIN, login, desktop, logout, guest, IRQ12 e clique do mouse que abre Files. O teste captura o framebuffer real e publica `build/orbis-gui-test.json`, `build/orbis-gui-setup-v2.ppm`, `build/orbis-gui-login-v2.ppm` e `build/orbis-gui-desktop.ppm`. As inspeções visuais desta revisão estão em `build/orbis-gui-setup-v11.png`, `build/orbis-gui-desktop-v11.png` e o atlas normalizado em `build/orbis-gui-preview/orbis-icons-atlas.png`. O launcher visível abre em tela cheia, sem menu ou abas do QEMU; `--windowed` mantém uma janela comum. Use `Ctrl+Alt+G` para liberar a captura de entrada e `Alt+F4` para fechar o QEMU.

No VirtualBox, a VM `ORBIS OS Preview` usa EFI64, 2 GiB de RAM, um vCPU, VBoxSVGA, vídeo 1280×800 e dispositivos PS/2. A preferência `GUI/Fullscreen=true` faz o frontend abrir em tela cheia; a tecla Host padrão, `Ctrl` direito, combinada com `F` alterna esse modo.

## Segurança e outras limitações

O cadastro e o PIN são **temporários, armazenados apenas em RAM**, sem PBKDF/KDF e sem autenticação segura. A GUI não é uma fronteira entre usuários. A tela Files ainda exibe pastas de demonstração e **não** monta o VFS real, e o item Terminal não lança aplicativos. Data e hora só podem ser exibidas quando houver relógio real integrado; não afirmar hora simulada como real. A imagem ainda não corresponde pixel a pixel aos conceitos gerados, que possuem widgets, fontes e iluminação mais avançados.

A versão Foundation e a ISO Winux 0.38 devem continuar com seus testes e hashes originais; o gate de IRQ12 e os assets acima são compilados apenas na variante GUI.
