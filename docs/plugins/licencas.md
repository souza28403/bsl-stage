# Licenças: publicar o seu plugin sem problemas

> Este documento é um guia prático, **não é aconselhamento jurídico**. Se o plugin for comercial ou houver dinheiro envolvido, consulte um advogado.

## O que é seu

O que você escreve do zero (o código GLSL, o cabeçalho, as imagens que você mesmo fez) é seu. Quem decide a licença é você. O BSL Stage não reivindica nada sobre plugins feitos pelos clientes: o programa só os lê.
Os modelos do assistente "Novo plugin…" e da pasta `skills/bsl-stage-plugin/templates` são de uso livre (CC0): você pode usá-los como ponto de partida e licenciar o resultado como quiser.

## O que NÃO é seu

Não use no seu plugin, sem permissão:

- **Código ou shaders de outras pessoas.** Achar um shader numa galeria da internet, num fórum ou num repositório não significa que você pode copiá-lo. Sem uma licença escrita permitindo, valem todos os
  direitos reservados do autor. Muitas galerias de shaders on-line aplicam por padrão uma licença como **CC BY-NC-SA** (exige dar crédito, **proíbe uso comercial** e obriga a publicar o derivado sob a mesma licença): um shader assim
  não pode entrar num plugin que você venda, nem ser relicenciado por você.
- **Plugins de terceiros.** Não copie, converta nem redistribua DLLs, shaders ou presets de outros. O BSL Stage não redistribui plugins de terceiros e você também não deve. Use os que você tem direito de usar, na sua
  máquina, conforme a licença de cada um.
- **Imagens, texturas, fontes e logotipos de terceiros** dentro do pacote.
- **Marcas.** Não use nomes e logotipos de outras empresas e produtos no nome do plugin.
- **Engenharia reversa de código.** Entender o que um programa faz olhando o resultado é comportamento; copiar ou traduzir código descompilado não é permitido.

Se você se inspirou numa ideia (por exemplo, "um efeito de ondas"), escreva a sua própria implementação. Ideias e técnicas matemáticas gerais (seno, ruído, desfoque gaussiano) são livres; a **expressão** (o código) de outra pessoa não.

Se usar código de terceiros que **permite** o uso (MIT, CC0, CC BY, domínio público...), siga o que a licença pede: normalmente manter o nome do autor e o texto da licença. Anote no campo `CREDIT` do cabeçalho e, se a licença exigir, inclua o texto no pacote.

## Escolhendo uma licença para o seu plugin

| Licença | O que permite | O que exige |
|---|---|---|
| **CC0 / domínio público** | Tudo, inclusive vender, sem pedir nada | Nada |
| **MIT** | Usar, copiar, modificar e vender | Manter o aviso de copyright e o texto da licença |
| **CC BY 4.0** | Usar, modificar e vender | Dar crédito a você |
| **CC BY-SA 4.0** | Usar, modificar e vender | Crédito e manter a mesma licença nos derivados |
| **CC BY-NC 4.0** | Usar e modificar, **sem fins comerciais** | Crédito |
| **GPL-3.0** | Usar, modificar e vender | Quem distribuir o derivado precisa publicar o código sob GPL |
| **Todos os direitos reservados** | Só o uso que você autorizar por escrito | Autorização para qualquer outro uso |

Dica: para compartilhar livremente com a comunidade, **MIT** ou **CC BY 4.0**. Para impedir que vendam o seu trabalho, **CC BY-NC 4.0**. Para vender você mesmo, **todos os direitos reservados** e um contrato de uso.

O BSL Stage é GPL-3.0. Um shader ISF ou um pacote `.bslplugin` é um arquivo de dados carregado pelo programa; você pode distribuí-lo com a licença que quiser. Uma DLL FFGL que você escreve é um programa à parte: a licença dela é sua,
mas o FFGL SDK que você usa para compilá-la é BSD de 3 cláusulas (mantenha o aviso dele).

## Como publicar

1. **Escolha a licença** e escreva o autor em `CREDIT` (ISF) ou no campo "autor" da DLL.
2. **Exporte um pacote**: aba Efeitos, botão direito sobre o plugin > **Exportar .bslplugin…**. Preencha nome, autor, versão, **licença** e descrição. O pacote leva o `.fs`, as imagens que ele importa e o `manifest.json`.
3. **Inclua o texto da licença** se ela exigir (MIT e outras): o pacote guarda o nome da licença no `manifest.json`; o texto completo vai na página onde você publicar, ou escreva-o no campo `CREDIT` do cabeçalho do shader (uma linha com o aviso de copyright).
4. **Publique** onde quiser (site, GitHub, drive). Não ponha dados pessoais nem caminhos do seu computador no pacote.
5. Quem **importar** o pacote vê o nome, o autor, a versão e a **licença que você declarou** antes de instalar, e só instala se aceitar.

O que o importador recusa por segurança: caminhos com `..`, arquivos que não sejam shader, imagem ou texto (programas, DLLs, scripts), pacotes enormes ou com arquivos disfarçados. Um plugin ISF não executa código no seu computador: ele só desenha na placa de vídeo.

## Para quem usa plugins de outras pessoas

- Leia a licença que aparece antes de instalar um `.bslplugin`.
- Plugins FFGL são programas: instale só de fontes confiáveis.
- Se a licença for NC (não comercial), não use o plugin em shows pagos ou produtos comerciais.
- Se o autor pedir crédito, dê o crédito (nome na tela, nos créditos do evento ou na descrição).
