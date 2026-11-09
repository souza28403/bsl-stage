# Stream de rede

Fonte ao vivo que traz uma transmissão pela rede para um clip. Aba **Fontes > Stream de rede** (arraste para uma célula ou dê duplo
clique): a janela **Stream de rede** abre sozinha. Depois ela reabre pelo botão "Abrir janela do stream" do painel Clip.

![Janela Stream de rede](screenshot-stream.png)

## Dois modos

**Abrir link.** Cole o endereço e dispare o clip. Protocolos aceitos:

| Endereço | Para quê | Atraso esperado |
|---|---|---|
| `rtmp://servidor/app/chave`, `rtmps://…` | servidor de transmissão | 0,5 a 3 s (depende do servidor) |
| `rtsp://usuario:senha@192.168.0.20/stream1` | câmera IP (usa TCP) | 0,2 a 1 s |
| `srt://servidor:9000` | contribuição de baixa latência, também pela internet | 0,2 a 0,5 s (o `latency` do SRT soma) |
| `udp://0.0.0.0:5000` | MPEG-TS enviado para esta máquina | 0,1 a 0,5 s |
| `https://servidor/lista.m3u8` | HLS | 5 a 30 s (inevitável no protocolo) |

Só esses protocolos entram: `file://` e semelhantes são recusados. A chave da transmissão fica escondida no campo (use o olho para
mostrar); o arquivo do projeto guarda o endereço completo, inclusive a chave, e o nome do clip e o log só mostram servidor e porta.

**Receber transmissão.** O BSL Stage escuta numa porta e mostra o que um programa de transmissão ou um celular enviar. A janela
lista os endereços prontos para copiar, um por interface de rede ativa:

- RTMP (padrão, porta 1935): `rtmp://<IP do PC>:1935/live/<nome>`. No programa de transmissão o servidor é `rtmp://<IP>:1935/live` e
  a chave é o `<nome>`. O BSL Stage aceita qualquer nome: o caminho serve para você identificar o envio (use portas diferentes
  para clips diferentes).
- SRT (porta 9000, modo ouvinte): `srt://<IP do PC>:9000`. No programa de transmissão escolha SRT no modo chamador (caller).

Um envio por clip, uma porta por clip. A fonte só escuta enquanto o clip está disparado (ou em segundo plano). Quando o envio termina
ela volta a escutar sozinha.

**Firewall do Windows.** O modo Receber escuta em todas as interfaces (`0.0.0.0`). Na primeira vez o Windows pede liberação
no firewall: permita em redes privadas. O BSL Stage nunca altera regras de firewall nem configurações do sistema.

## Estado e reconexão

O painel Clip e a janela mostram: `Conectando…`, `Aguardando envio em <endereço>`, `Ao vivo – 1920x1080 · 30 fps · H264 · 4.2 Mbps`,
`Sem sinal – tentando de novo em N s` e `Erro: <mensagem>` (endereço inválido, porta em uso, protocolo não suportado…).

- Se não conecta ou o sinal cai, tenta de novo com espera crescente: 1, 2, 4, 8 e depois 10 s. Sem dados por 5 s (20 s em HLS) conta
  como queda. Conectar numa porta fechada pode levar até 5 s para falhar (comportamento do Windows).
- **Ao perder o sinal**: *Manter última imagem* (padrão) ou *Preto*. A miniatura da grade mostra o último quadro.
- Fechar o clip, trocar de clip ou fechar o app nunca espera a rede: todas as chamadas de rede podem ser interrompidas e a parada
  leva menos de 0,5 s.

## Dicas para o programa de transmissão

- **Keyframe a cada 1 a 2 s** (intervalo de GOP = 1 a 2 × fps). Para baixa latência o BSL Stage descarta o que veio antes do primeiro
  keyframe, então um GOP longo atrasa o primeiro quadro.
- **Sem B-frames** (menor atraso), perfil Baseline ou Main/High sem B.
- 1080p30 ou 1080p60, H.264 de 4 a 10 Mb/s. Fluxos maiores pedem CPU: o decodificador é por software.
- Áudio é ignorado por enquanto (só vídeo).

## Limitações conhecidas

- Decodificação por CPU (o decodificador de vídeo do app ainda não usa aceleração por hardware).
- Sem áudio: os clips de vídeo do app também não tocam áudio.
- Soltar na grade só funciona com **atalhos da Internet (`.url`)** de endereços rtmp, rtsp, srt, udp ou http(s): o app recebe
  arquivos soltos, não texto.
- O caminho/nome RTMP não filtra quem envia.

## Teste automático

`build\Release\bslstage_streamtest.exe` roda só em `127.0.0.1` (não aciona o firewall), com vídeo sintético: recebe RTMP, puxa RTMP,
SRT e UDP, queda e reconexão, parada em menos de 500 ms durante bloqueio, URL inválida, porta em uso e projeto salvo/carregado. O passo
`stream` do `tools/uiqa/run.py` abre a janela e gera `docs/screenshot-stream.png`.
