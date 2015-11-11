#ifndef BARCHART_DEF_H
#define BARCHART_DEF_H 1

struct ag_func
{
  const char *name;
  const char *description;
  
  int arity;
  bool cumulative;
  double (*pre) (void);
  double (*calc) (double acc, double x, double w);
  double (*post) (double acc, double cc);
  double (*ppost) (double acc, double ccc);
};

extern const struct ag_func ag_func[];

extern const int N_AG_FUNCS;

#endif
