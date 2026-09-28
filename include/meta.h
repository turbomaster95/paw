#ifndef META_H
#define META_H

typedef struct PawMeta {
  int major;
  int minor;
  int patch;

  const char *koolname;
  const char *gccver;
  const char *builddate;
  const char *gitbranch;
} PawMeta;

extern const PawMeta g_meta;

#endif
