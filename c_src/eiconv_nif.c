/* 
 * eiconv -- an erlang iconv nif.
 */

#include <iconv.h>
#include <string.h>
#include <errno.h>
#include <erl_nif.h>

#include <stdio.h>

#define CONV_DESC_LEN 80

static ErlNifResourceType *eiconv_cd_type = NULL;

static ERL_NIF_TERM _atom_ok;
static ERL_NIF_TERM _atom_done;
static ERL_NIF_TERM _atom_more;
static ERL_NIF_TERM _atom_error;
static ERL_NIF_TERM _atom_enomem;
static ERL_NIF_TERM _atom_eilseq;
static ERL_NIF_TERM _atom_einval;
static ERL_NIF_TERM _atom_eunknown;

typedef struct { 
    iconv_t cd; 
} eiconv_cd;

static void descruct_eiconv_cd(ErlNifEnv *env, void *cd)
{
    /* When cd is -1 when iconv_open failed, and some versions of 
     * iconv_close will fail.
     */
    if (((eiconv_cd *) cd)->cd != (iconv_t)(-1))
        iconv_close(((eiconv_cd *) cd)->cd);
}

static ERL_NIF_TERM 
eiconv_make_error(ErlNifEnv* env, int error_number) {
    ERL_NIF_TERM error;

    if(error_number == EILSEQ) {
        error = _atom_eilseq;
    } else if (error_number == EINVAL) {
        error = _atom_einval;
    } else {
        error = _atom_eunknown;
    }

    return enif_make_tuple2(env, _atom_error, error);
}
 
static ERL_NIF_TERM 
eiconv_open_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) 
{
    char tocode[CONV_DESC_LEN], fromcode[CONV_DESC_LEN];
    ErlNifBinary tocode_bin, fromcode_bin;
    eiconv_cd *cd;
    ERL_NIF_TERM conv_d;

    if (!enif_inspect_iolist_as_binary(env, argv[0], &tocode_bin))
        return enif_make_badarg(env);

    if (!enif_inspect_iolist_as_binary(env, argv[1], &fromcode_bin))
        return enif_make_badarg(env);

    if (tocode_bin.size >= CONV_DESC_LEN-1)
        return enif_make_badarg(env);

    if (fromcode_bin.size >= CONV_DESC_LEN-1)
        return enif_make_badarg(env);

    memcpy((void*) tocode, tocode_bin.data, tocode_bin.size);
    tocode[tocode_bin.size] = '\0';

    memcpy((void *) fromcode, fromcode_bin.data, fromcode_bin.size);
    fromcode[fromcode_bin.size] = '\0';

    /* Allocate the resource
    */
    cd = enif_alloc_resource(eiconv_cd_type, sizeof(eiconv_cd));
    cd->cd = iconv_open(tocode, fromcode);
    if((iconv_t)(-1) == cd->cd) {
        enif_release_resource(cd);
        return eiconv_make_error(env, EINVAL);
    }

    conv_d = enif_make_resource(env, cd);
    enif_release_resource(cd); 

    return enif_make_tuple2(env, _atom_ok, conv_d);
}

static ERL_NIF_TERM 
eiconv_conv_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) 
{
    eiconv_cd *cd;
    ErlNifBinary inbuf, outbuf;
    unsigned char *in, *out;
    size_t inbytesleft, outbytesleft, outbufsize, rc;

    if (!enif_get_resource(env, argv[0], eiconv_cd_type, (void **) &cd))
        return enif_make_badarg(env);

    if (((eiconv_cd *) cd)->cd == (iconv_t)(-1))
        return enif_make_badarg(env);
  
    if (!enif_inspect_iolist_as_binary(env, argv[1], &inbuf))
        return enif_make_badarg(env);

    in = inbuf.data;
    inbytesleft = inbuf.size;

    outbufsize = inbytesleft + (inbytesleft/2); 
    outbytesleft = outbufsize;

    if(!enif_alloc_binary(outbufsize, &outbuf)) 
        return enif_make_tuple2(env, _atom_error, _atom_enomem);

    out = outbuf.data;
    
    iconv(cd->cd, NULL, NULL, NULL, NULL);

    do {
        rc = iconv(cd->cd, (char **) &in, &inbytesleft, (char **) &out, &outbytesleft);
        if(rc == 0) break;

        if (errno == E2BIG) {
            outbytesleft += outbufsize;
            outbufsize *= 2;

            if (!enif_realloc_binary(&outbuf, outbufsize)) {
	           enif_release_binary(&outbuf);
	           return enif_make_tuple2(env, _atom_error, _atom_enomem);
            }

            out = outbuf.data + (outbufsize - outbytesleft);
        } else {
            enif_release_binary(&outbuf);
            return eiconv_make_error(env, errno);
        }
    } while (rc != 0);

    if(outbytesleft > 0)
        enif_realloc_binary(&outbuf, outbufsize - outbytesleft);

    return enif_make_tuple2(env, _atom_ok, enif_make_binary(env, &outbuf));
}

static ERL_NIF_TERM 
eiconv_chunk_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) 
{
    eiconv_cd *cd;
    ErlNifBinary inbuf, outbuf;
    unsigned char *in, *out;
    size_t inbytesleft, outbytesleft, outbufsize, rc;

    if (!enif_get_resource(env, argv[0], eiconv_cd_type, (void **) &cd))
        return enif_make_badarg(env);

    if (((eiconv_cd *) cd)->cd == (iconv_t)(-1))
        return enif_make_badarg(env);
  
    if (!enif_inspect_iolist_as_binary(env, argv[1], &inbuf))
        return enif_make_badarg(env);

    in = inbuf.data;
    inbytesleft = inbuf.size;

    outbufsize = inbytesleft + (inbytesleft/2); 
    outbytesleft = outbufsize;

    if(!enif_alloc_binary(outbufsize, &outbuf)) 
        return enif_make_tuple2(env, _atom_error, _atom_enomem);

    out = outbuf.data;
    
    do {
        rc = iconv(cd->cd, (char **) &in, &inbytesleft, (char **) &out, &outbytesleft);
        if(rc == 0) 
            break;

        if (errno == E2BIG) {
            /* The result doesn't fit in outbuf, realloc and try again */
            outbytesleft += outbufsize;
            outbufsize *= 2;

            if (!enif_realloc_binary(&outbuf, outbufsize)) {
               enif_release_binary(&outbuf);
               return enif_make_tuple2(env, _atom_error, _atom_enomem);
            }

            out = outbuf.data + (outbufsize - outbytesleft);
        } else if (errno == EINVAL) {
            /* we got an incomplete character sequence */
            if(outbytesleft > 0)
                enif_realloc_binary(&outbuf, outbufsize - outbytesleft);
            return enif_make_tuple2(env, _atom_more, enif_make_binary(env, &outbuf));
        } else {
            enif_release_binary(&outbuf);
            return eiconv_make_error(env, errno);
        }
    } while (rc != 0);

    if(outbytesleft > 0)
        enif_realloc_binary(&outbuf, outbufsize - outbytesleft);

    return enif_make_tuple2(env, _atom_done, enif_make_binary(env, &outbuf));
}

/* 
 * Set the cd structure to its initial state.
 */
static ERL_NIF_TERM 
eiconv_reset_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) 
{
    eiconv_cd *cd;
    ErlNifBinary outbuf;
    unsigned char *out;
    size_t rc, outbytesleft=1024, outbufsize=1024;
 
    if (!enif_get_resource(env, argv[0], eiconv_cd_type, (void **) &cd))
        return enif_make_badarg(env);

    if (((eiconv_cd *) cd)->cd == (iconv_t)(-1))
        return enif_make_badarg(env);

    if(!enif_alloc_binary(outbufsize, &outbuf)) 
        return enif_make_tuple2(env, _atom_error, _atom_enomem);

    /* Reset the cd structure */
    out = outbuf.data;
    rc = iconv(cd->cd, NULL, NULL, (char **) &out, &outbytesleft);
    if(rc != 0) {
        enif_release_binary(&outbuf);
        return eiconv_make_error(env, errno);
    }

    /* Return leftover bytes */
    if(outbytesleft > 0)
        enif_realloc_binary(&outbuf, outbufsize - outbytesleft);

    return enif_make_tuple2(env, _atom_ok, enif_make_binary(env, &outbuf));
}


/* 
 * loading
 */

static int 
on_load(ErlNifEnv* env, void** priv, ERL_NIF_TERM info)
{
    ErlNifResourceType *rt = enif_open_resource_type(
        env, "eiconv", "eiconv_cd_type", 
        descruct_eiconv_cd, ERL_NIF_RT_CREATE, NULL);
    if(!rt) 
        return -1;

    eiconv_cd_type = rt;

    /* Create some atoms 
    */
    _atom_ok = enif_make_atom(env, "ok");
    _atom_more = enif_make_atom(env, "more");
    _atom_done = enif_make_atom(env, "done");
    _atom_error = enif_make_atom(env, "error");
    _atom_enomem = enif_make_atom(env, "enomem");
    _atom_eilseq = enif_make_atom(env, "eilseq");
    _atom_einval = enif_make_atom(env, "einval");
    _atom_eunknown = enif_make_atom(env, "eunknown");
  
    return 0;
}

static ErlNifFunc nif_funcs[] = {
    {"open", 2, eiconv_open_nif},
    {"conv", 2, eiconv_conv_nif},
    {"chunk", 2, eiconv_chunk_nif},
    {"reset", 1, eiconv_reset_nif}
};

ERL_NIF_INIT(eiconv, nif_funcs, on_load, NULL, NULL, NULL);
