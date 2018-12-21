/*
 * a tool that finds dynamic symbol conflicts at runtime
 *
 * how to use this?
 * compile this with:
 *  gcc dlconflict.c -shared -o dlconflict.so
 *
 * then whenever you want to test for symbol conflict 
 * (ideally after all other libraries are loaded, this happens before main)
 * you just need to load 'dlconflict.so' using dlopen
 * the rest is handled by a constructor function that runs on loading the library
 * a dump of the results will be printed on stderr
 * you might need to filter this again to ignore libc (it's a mess)
 *
 * #include <dlfcn.h>
 * int main(void)
 * {
 *      void *h = dlopen("path_to/dlconflict.so", RTLD_LAZY);
 *      if (!h){
 *          fprintf(stderr, "failed to load dlconflict.so");
 *      }
 * }
 *
 *
 * author: github/nilputs
 * license: GPL v3
 *
 * helpful resources:
 *
 * https://flapenguin.me/2017/05/10/elf-lookup-dt-gnu-hash/
 * https://stackoverflow.com/a/27304692/10438632
 * https://stackoverflow.com/a/29911465/10438632
 * */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/auxv.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <stddef.h>
#include <limits.h>
#include <ctype.h>


#define STT_TYPE 1
#define STB_BIND 2


static void vdie(char *msg)
{
    fprintf(stderr, msg);
    exit(1);
}

static const char *constant_to_str(int kind, int constant)
{
    if (kind == STB_BIND){
        switch(constant){
            case STB_LOCAL	: return "STB_LOCAL";
            case STB_GLOBAL	: return "STB_GLOBAL";
            case STB_WEAK	: return "STB_WEAK";
            case STB_NUM		: return "STB_NUM";
            case STB_LOOS	: return "STB_LOOS";
            case STB_HIOS: return "STB_HIOS";
            case STB_LOPROC: return "STB_LOPROC";
            case STB_HIPROC: return "STB_HIPROC";
        }
    }
    else if (kind == STT_TYPE){
        switch(constant){
            case STT_NOTYPE	: return "STT_NOTYPE";
            case STT_OBJECT	: return "STT_OBJECT";
            case STT_FUNC	: return "STT_FUNC";
            case STT_SECTION	: return "STT_SECTION";
            case STT_FILE	: return "STT_FILE";
            case STT_COMMON	: return "STT_COMMON";
            case STT_TLS		: return "STT_TLS";
            case STT_NUM		: return "STT_NUM";
            case STT_LOOS	: return "STT_LOOS";
            case STT_HIOS	: return "STT_HIOS";
            default:
                if (constant > STT_LOPROC && constant <= STT_HIPROC)
                    return "STT_LOPROC - STT_HIPROC";
        }
    }

    return "UNKNOWN";
}


struct entry{
    const char *src;
    const char *name;
    unsigned char type;
    unsigned char bind;
    unsigned char is_defined;
};

struct entry_vec{
    struct entry *data;
    int n;
    int cap;
};

static int cmp_entry(struct entry *left, struct entry *right);

static void swap_entry(struct entry *left, struct entry *right)
{
    struct entry tmp;
    memmove(&tmp, left,  sizeof(struct entry));
    memmove(left, right, sizeof(struct entry));
    memmove(right, &tmp, sizeof(struct entry));
}

int entry_partition (struct entry *arr, int low, int high) 
{ 
    struct entry *pivot = arr + high;
    int i = (low - 1);
  
    for (int j = low; j <= high - 1; j++){ 
        if (cmp_entry(arr+j, pivot) < 0){ 
            i++;
            swap_entry(arr+i, arr+j);
        }
    }
    swap_entry(arr+i+1, pivot);
    return i + 1; 
}
  
void entry_quicksort(struct entry *arr, int low, int high) 
{ 
    if (low >= high)
        return;
    int pi = entry_partition(arr, low, high); 
    entry_quicksort(arr, low,    pi - 1); 
    entry_quicksort(arr, pi + 1, high); 
} 


static void entry_vec_push(struct entry_vec *v, struct entry *e)
{
    if (v->n + 1 > v->cap){
        v->cap *= 2;
        v->data = realloc(v->data, sizeof(struct entry) * v->cap);
        if(!v->data)
            vdie("realloc: no mem");
    }
    memcpy(v->data + v->n++, e, sizeof(struct entry));
}

static void entry_vec_init(struct entry_vec *v){
    v->data = malloc(sizeof(struct entry) * 8);
    if(!v->data)
        vdie("malloc: nomem");
    v->cap = 8;
    v->n = 0;
}
static void entry_vec_destroy(struct entry_vec *v){
    free(v->data);
    v->data = NULL;
}


static int cmp_entry(struct entry *left, struct entry *right)
{
    // tuple: name, bind, is_defined, type 
    int a = strcmp(left->name, right->name);
    int b = left->bind - right->bind;
    int c = left->is_defined - right->is_defined;
    int d = left->type - right->type;
    if (a)
        return a;
    if (b)
        return b;
    if (c)
        return c;
    return d;
}

//we can change this function to query on different things, 
//here we're interested in global symbols where a definition is provided for them 
//(if a definition wasn't provided then it means the object is looking for the symbol)
// we're assuming they're contigious, you may need to change the sort comparison function cmp_entry() or do a full O(n^2) search
// if you change the conflict condition
static int conflict_condition(struct entry *a, struct entry *b)
{
    return strcmp(a->name, b->name) == 0 &&
           a->bind == b->bind &&
           a->bind == STB_GLOBAL &&
           a->is_defined;
}

static void entry_vec_stats(struct entry_vec *v){
    fprintf(stderr, "%d entries\n", v->n);
    for (int i=0; i < v->n - 1; i++){
        if (conflict_condition(v->data+i, v->data+i+1)){
            fprintf(stderr, "conflict between symbols:\n");
            int count = 1;
            for (int k = i; (k < v->n - 1) && conflict_condition(v->data+k, v->data+k+1); k++)
                count++;
            for (int j = 0; j < count; j++){
                    fprintf(stderr, "name: '%s' bind: '%s' type: '%s' lib:%s is_defined?: %d\n",
                        v->data[i+j].name,
                        constant_to_str(STB_BIND, v->data[i+j].bind),
                        constant_to_str(STT_TYPE, v->data[i+j].type),
                        v->data[i+j].src,
                        !!v->data[i+j].is_defined);
            }
            fprintf(stderr, "\n\n");
            i += count;
        }
    }
}

static ElfW(Word) gnu_hashtab_symbol_count(const uint32_t *const table)
{
    uint32_t max = 0;
    uint32_t nbuckets = table[0];
    uint32_t symoffset = table[1];
    uint32_t bloomsize = table[2];
    uint32_t bloomshift = table[3];
    (void) bloomshift;
#if __WORDSIZE == 64
    uint32_t *buckets = (uint32_t *)(((char *)(table + 4)) + (bloomsize * sizeof(uint64_t)));
#elif __WORDSIZE == 32
    /*untested*/
    uint32_t *buckets = (uint32_t *)(((char *)(table + 4)) + (bloomsize * sizeof(uint32_t)));
#else
    #error unsupported
#endif
    uint32_t *chains = buckets + nbuckets;

    for (int i=0; i<nbuckets; i++){
        int bucket_max = buckets[i];
        int first = buckets[i] - symoffset;
        int even = 0;
        for (int j=first; (chains[j] & 1) == 0; j++){
            even++;
        }
        bucket_max += even;
        max = (bucket_max > max) ? bucket_max : max;
    }

    return (ElfW(Word)) max;
}


static void *dynamic_pointer(const ElfW(Addr) addr, const ElfW(Addr) base, const ElfW(Phdr) *const header, const ElfW(Half) headers)
{
    if (addr) {
        ElfW(Half) h;

        for (h = 0; h < headers; h++)
            if (header[h].p_type == PT_LOAD)
                if (addr >= base + header[h].p_vaddr &&
                    addr <  base + header[h].p_vaddr + header[h].p_memsz)
                    return (void *)addr;
    }
    return NULL;
}


static int callback(struct dl_phdr_info *info, size_t size, void *dataref)
{
    const ElfW(Addr)                 base = info->dlpi_addr;
    const ElfW(Phdr) *const          header = info->dlpi_phdr;
    const ElfW(Half)                 headers = info->dlpi_phnum;
    ElfW(Half)                       h;

    fprintf(stderr, "inspecting the library: '%s'\n", info->dlpi_name);

    struct entry_vec *vec = dataref;

    for (h = 0; h < headers; h++){

        if (header[h].p_type != PT_DYNAMIC)
            continue;

        const ElfW(Dyn)  *entry = (const ElfW(Dyn) *)(base + header[h].p_vaddr);
        const ElfW(Word) *hashtab;
        const ElfW(Sym)  *symtab = NULL;
        const char       *strtab = NULL;
        ElfW(Word)        symbol_count = 0;

        for (; entry->d_tag != DT_NULL; entry++){
            switch (entry->d_tag) {
                case DT_HASH:
                    hashtab = dynamic_pointer(entry->d_un.d_ptr, base, header, headers);
                    if (hashtab){
                        ElfW(Word) count = hashtab[1];
                        symbol_count = count > symbol_count ? count : symbol_count;
                    }
                    break;
                case DT_GNU_HASH:
                    hashtab = dynamic_pointer(entry->d_un.d_ptr, base, header, headers);
                    if (hashtab) {
                        ElfW(Word) count = gnu_hashtab_symbol_count(hashtab);
                        if (count > symbol_count)
                            symbol_count = count;
                    }
                    break;
                case DT_STRTAB:
                    strtab = dynamic_pointer(entry->d_un.d_ptr, base, header, headers);
                    break;
                case DT_SYMTAB:
                    symtab = dynamic_pointer(entry->d_un.d_ptr, base, header, headers);
                    break;
            }
        }

        if (!symtab || !strtab || !symbol_count)
            continue; 

        for (ElfW(Word)  s = 0; s <= symbol_count; s++) {
            void *const ptr = dynamic_pointer(base + symtab[s].st_value, base, header, headers);

            if (!ptr)
                continue;

#if __WORDSIZE == 64
            int bind = ELF64_ST_BIND(symtab[s].st_info);  
            int type = ELF64_ST_TYPE(symtab[s].st_info); 
#elif __WORDSIZE == 32
            int bind = ELF32_ST_BIND(symtab[s].st_info);  
            int type = ELF32_ST_TYPE(symtab[s].st_info); 
#else
    #error unsupported
#endif
            int is_defined = symtab[s].st_shndx;
            const char * name = strtab + symtab[s].st_name;
            struct entry ent = {
               .src =  info->dlpi_name,
               .name = name,
               .bind = bind,
               .type = type,
               .is_defined = is_defined
            };
            entry_vec_push(vec, &ent);
        }
    }

    return 0;
}


__attribute__((constructor)) static void init(void) {
    struct entry_vec vec;
    entry_vec_init(&vec);

    dl_iterate_phdr(callback, &vec);

    entry_quicksort(vec.data, 0, vec.n - 1);
    entry_vec_stats(&vec);
    entry_vec_destroy(&vec);
}
