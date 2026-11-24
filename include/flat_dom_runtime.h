#ifndef FLAT_DOM_RUNTIME_H
#define FLAT_DOM_RUNTIME_H

struct FlatDomRuntime;
struct FlatDomRuntime *flat_dom_runtime_create(void);
void flat_dom_runtime_destroy(struct FlatDomRuntime *runtime);

#endif
