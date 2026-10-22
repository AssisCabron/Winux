# ORBIS OS — GUI nativa, tipografia proporcional e mouse IRQ12

Data: 22/10/2026. Estado: **preview experimental QEMU/UEFI**, não promovido à ISO Winux 0.38 estável.

## Correção da interface

O código de interface é executado diretamente no framebuffer UEFI GOP do kernel em `src/ui/orbis_gui.h`. Não há navegador, Electron ou HTML.

- **Fonte:** `scripts/build/generate_orbis_font.ps1` rasteriza caracteres ASCII 32–126 a partir da fonte Segoe UI do Windows autorizado, gerando bitmap **8 bpp com cobertura de antialiasing**, remoção de margem lateral e avanço de largura diferente por caractere. O artefato compilado está em `build/orbis-gui-preview/orbis_font.h`; **não copiar ou distribuir arquivos de fonte**.
- **Wallpaper:** `scripts/build/generate_orbis_rgb888.ps1` prepara uma fotografia panorâmica a 1280×800 RGB888, sem RGB565 ou desfoque de ampliação na resolução QEMU utilizada. O original em `src/ui/assets/orbis-wallpaper-golden.jpg` é uma fotografia temporária de referência, não exatamente a arte gerada anteriormente no ChatGPT. O recurso de 3.072.000 bytes é embutido via assembler `.incbin` e objeto COFF; a ISO estável não é alterada. Para usar um wallpaper diferente, substitua legalmente a fonte, preserve a proporção e recompile.
- **Painéis:** composição com alpha em software, sombras e contornos arredondados. O compositor geral de janelas, vetores SVG, animações, GPU e fontes internacionais completas ainda não estão implementados.

## Mouse responsivo

A implementação anterior lia PS/2 apenas no loop de polling e até desenhava o cursor dentro de `CLI`. Isso causava latência e prejudicava interrupções durante operações gráficas.

Agora `src/arch/x86_64/hardware.h` instala gate de IRQ12 (vetor 44) **somente com WINUX_ORBIS_GUI_PREVIEW**, recebe o byte auxiliar do i8042 e o coloca em uma fila circular de 128 bytes, fazendo EOI no PIC escravo e mestre. O frame da interrupção salva/restaura registradores usados. Não são feitas pinturas na ISR.

Em `src/ui/orbis_gui.h`, a inicialização habilita IRQ12 no controlador i8042 e nos dois PICs, e o loop lê a fila, reabilita interrupções e só então atualiza o cursor. Movimentos normais salvam/restauram somente 11×17 pixels. Clique pode causar redraw completo, mas nenhum redraw é feito em contexto de IRQ.

Limites: single CPU, PS/2 legado, sem USB HID, sem aceleração, sem compositor com dirty rectangles completo, sem medições de latência no computador físico.

## Testes

```powershell
python scripts/build/make_proc_space_preview.py --experiment integration --orbis-gui
python tests/runners/test_orbis_gui_qemu.py
python scripts/build/run_orbis_gui_visible.py
```

O teste automatizado usa QEMU separado em `-display none`, injeta scancodes pelo PS/2 real, valida boot splash, configuração local de RAM, rejeição de PIN, login, desktop, logout, guest, IRQ12 e clique do mouse que abre Files. O teste captura o framebuffer real e publica `build/orbis-gui-test.json` e `build/orbis-gui-desktop.ppm`. O script visível abre **uma janela GTK persistente** no desktop Windows e não a fecha automaticamente.

## Segurança e outras limitações

O cadastro e o PIN são **temporários, armazenados apenas em RAM**, sem PBKDF/KDF e sem autenticação segura. A GUI não é uma fronteira entre usuários. A tela Files ainda exibe pastas de demonstração e **não** monta o VFS real, e o item Terminal não lança aplicativos. Data e hora só podem ser exibidas quando houver relógio real integrado; não afirmar hora simulada como real. A imagem ainda não corresponde pixel a pixel aos conceitos gerados, que possuem widgets, fontes e iluminação mais avançados.

A versão Foundation e a ISO Winux 0.38 devem continuar com seus testes e hashes originais; o gate de IRQ12 e os assets acima são compilados apenas na variante GUI.
