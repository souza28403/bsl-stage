# Plugins FFGL de teste

Usados so pelo `bslstage_ffgltest`; nao vao no instalador.

- `sdk/`, `AddSubtract/`, `Gradients/`: copia sem alteracoes do FFGL SDK 2.x (BSD, ver `LICENSE-FFGL-SDK.md`) e de dois dos
  seus exemplos: um efeito (AddSubtract: cor RGB) e uma fonte (Gradients: RGBA + HSBA).
  O SDK usa o GLEW; aqui `shim/GL/glew.h` + `shim/glew_shim.c` o trocam por um carregador feito com o glad.
- `FailPlugin.cpp`: tres DLLs que se comportam mal (violacao de acesso no `FF_PROCESS_OPENGL`, trava no `FF_GET_INFO`,
  DLL sem `plugMain`) para provar que o host sobrevive.
