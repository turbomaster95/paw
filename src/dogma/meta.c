#include <stdio.h>
#include <meta.h>
#include <string.h>
#include <dogma.h>

char* dogma_get_gcc(void) {
  return (char*)g_meta.gccver;
}

char* dogma_get_compile_date(void) {
  return (char*)g_meta.builddate;
}

char* dogma_get_codename(void) {
  return (char*)g_meta.koolname;
}

char* dogma_get_fullver(void) {
  char ver[128];

  snprintf(ver, sizeof(ver), "%d.%d.%d", g_meta.major, g_meta.minor, g_meta.patch);

  return strdup(ver);
}

char* dogma_get_branch(void) {
  return (char*)g_meta.gitbranch;
}

char* dogma_get_platform(void) {
#ifndef WASI
  return "linux";
#else
  return "wasi";
#endif
}
