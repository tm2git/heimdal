/*
 * Copyright (c) 1997 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "krb5_locl.h"
#include "config_file.h"
RCSID("$Id$");

static int parse_section(char *p, krb5_config_section **s,
			 krb5_config_section **res);
static int parse_binding(FILE *f, unsigned *lineno, char *p,
			 krb5_config_binding **b,
			 krb5_config_binding **parent);
static int parse_list(FILE *f, unsigned *lineno, krb5_config_binding **parent);

static int
parse_section(char *p, krb5_config_section **s, krb5_config_section **parent)
{
    char *p1;
    krb5_config_section *tmp;

    p1 = strchr (p + 1, ']');
    if (p1 == NULL)
	return -1;
    *p1 = '\0';
    tmp = malloc(sizeof(*tmp));
    if (tmp == NULL)
	return -1;
    if (*s)
	(*s)->next = tmp;
    else
	*parent = tmp;
    *s = tmp;
    tmp->name = strdup(p+1);
    if (tmp->name == NULL)
	return -1;
    tmp->type = LIST;
    tmp->u.list = NULL;
    tmp->next = NULL;
    return 0;
}

static int
parse_list(FILE *f, unsigned *lineno, krb5_config_binding **parent)
{
    char buf[BUFSIZ];
    int ret;
    krb5_config_binding *b = NULL;

    for (; fgets(buf, sizeof(buf), f) != NULL; ++*lineno) {
	char *p;

	if (buf[strlen(buf) - 1] == '\n')
	    buf[strlen(buf) - 1] = '\0';
	p = buf;
	if (*p == '#')
	    continue;
	while(isspace(*p))
	    ++p;
	if (*p == '}')
	    return 0;
	ret = parse_binding (f, lineno, p, &b, parent);
	if (ret)
	    return ret;
    }
    return -1;
}

static int
parse_binding(FILE *f, unsigned *lineno, char *p,
	      krb5_config_binding **b, krb5_config_binding **parent)
{
    krb5_config_binding *tmp;
    char *p1;
    int ret;

    p1 = p;
    while (*p && !isspace(*p))
	++p;
    if (*p == '\0')
	return -1;
    *p = '\0';
    tmp = malloc(sizeof(*tmp));
    if (tmp == NULL)
	return -1;
    if (*b)
	(*b)->next = tmp;
    else
	*parent = tmp;
    *b = tmp;
    tmp->name = strdup(p1);
    tmp->next = NULL;
    ++p;
    while (isspace(*p))
	++p;
    if (*p != '=')
	return -1;
    ++p;
    while(isspace(*p))
	++p;
    if (*p == '{') {
	tmp->type = LIST;
	tmp->u.list = NULL;
	ret = parse_list (f, lineno, &tmp->u.list);
	if (ret)
	    return ret;
    } else {
	tmp->type = STRING;
	tmp->u.string = strdup(p);
    }
    return 0;
}

krb5_error_code
krb5_config_parse_file (const char *fname, krb5_config_section **res)
{
    FILE *f;
    krb5_config_section *s;
    krb5_config_binding *b;
    char buf[BUFSIZ];
    unsigned lineno;
    int ret;

    s = NULL;
    b = NULL;
    f = fopen (fname, "r");
    if (f == NULL)
	return -1;
    *res = NULL;
    for (lineno = 1; fgets(buf, sizeof(buf), f) != NULL; ++lineno) {
	char *p;

	if(buf[strlen(buf) - 1] == '\n')
	    buf[strlen(buf) - 1] = '\0';
	p = buf;
	if (*p == '#')
	    continue;
	while(isspace(*p))
	    ++p;
	if (*p == '[') {
	    ret = parse_section(p, &s, res);
	    if (ret)
		return ret;
	    b = NULL;
	} else if (*p == '}') {
	    return -1;
	} else if(*p != '\0') {
	    ret = parse_binding(f, &lineno, p, &b, &s->u.list);
	    if (ret)
		return ret;
	}
    }
    fclose (f);
    return 0;
}

static void
free_binding (krb5_config_binding *b)
{
    krb5_config_binding *next_b;

    while (b) {
	free (b->name);
	if (b->type == STRING)
	    free (b->u.string);
	else if (b->type == LIST)
	    free_binding (b->u.list);
	else
	    abort ();
	next_b = b->next;
	free (b);
	b = next_b;
    }
}

krb5_error_code
krb5_config_file_free (krb5_config_section *s)
{
    free_binding (s);
    return 0;
}

static int print_list (FILE *f, krb5_config_binding *l, unsigned level);
static int print_binding (FILE *f, krb5_config_binding *b, unsigned level);
static int print_section (FILE *f, krb5_config_section *s, unsigned level);
static int print_config (FILE *f, krb5_config_section *c);

static void
tab (FILE *f, unsigned count)
{
    while(count--)
	fprintf (f, "\t");
}

static int
print_list (FILE *f, krb5_config_binding *l, unsigned level)
{
    while(l) {
	print_binding (f, l, level);
	l = l->next;
    }
    return 0;
}

static int
print_binding (FILE *f, krb5_config_binding *b, unsigned level)
{
    tab (f, level);
    fprintf (f, "%s = ", b->name);
    if (b->type == STRING)
	fprintf (f, "%s\n", b->u.string);
    else if (b->type == LIST) {
	fprintf (f, "{\n");
	print_list (f, b->u.list, level + 1);
	tab (f, level);
	fprintf (f, "}\n");
    } else
	abort ();
    return 0;
}

static int
print_section (FILE *f, krb5_config_section *s, unsigned level)
{
    fprintf (f, "[%s]\n", s->name);
    print_list (f, s->u.list, level + 1);
    return 0;
}

static int
print_config (FILE *f, krb5_config_section *c)
{
    while (c) {
	print_section (f, c, 0);
	c = c->next;
    }
    return 0;
}

const void *
krb5_config_get_next (krb5_config_section *c,
		      krb5_config_binding **pointer,
		      int type,
		      ...)
{
    const char *ret;
    va_list args;

    va_start(args, type);
    ret = krb5_config_vget_next (c, pointer, type, args);
    va_end(args);
    return ret;
}

const void *
krb5_config_vget_next (krb5_config_section *c,
		       krb5_config_binding **pointer,
		       int type,
		       va_list args)
{
    krb5_config_binding *b;
    const char *p;

    if(c == NULL)
	return NULL;

    if (*pointer == NULL) {
	b = c;
	p = va_arg(args, const char *);
	if (p == NULL)
	    return NULL;
    } else {
	b = *pointer;
	p = b->name;
	b = b->next;
    }

    while (b) {
	if (strcmp (b->name, p) == 0) {
	    if (*pointer == NULL)
		p = va_arg(args, const char *);
	    else
		p = NULL;
	    if (type == b->type && p == NULL) {
		*pointer = b;
		return b->u.generic;
	    } else if(b->type == LIST && p != NULL) {
		b = b->u.list;
	    } else {
		return NULL;
	    }
	} else {
	    b = b->next;
	}
    }
    return NULL;
}

const void *
krb5_config_get (krb5_config_section *c,
		 int type,
		 ...)
{
    const void *ret;
    va_list args;

    va_start(args, type);
    ret = krb5_config_vget (c, type, args);
    va_end(args);
    return ret;
}

const void *
krb5_config_vget (krb5_config_section *c,
		  int type,
		  va_list args)
{
    krb5_config_binding *foo = NULL;

    return krb5_config_vget_next (c, &foo, type, args);
}

const krb5_config_binding *
krb5_config_get_list (krb5_config_section *c,
		      ...)
{
    const krb5_config_binding *ret;
    va_list args;

    va_start(args, c);
    ret = krb5_config_vget_list (c, args);
    va_end(args);
    return ret;
}

const krb5_config_binding *
krb5_config_vget_list (krb5_config_section *c,
		       va_list args)
{
    return krb5_config_vget (c, LIST, args);
}

const char *
krb5_config_get_string (krb5_config_section *c,
			...)
{
    const char *ret;
    va_list args;

    va_start(args, c);
    ret = krb5_config_vget_string (c, args);
    va_end(args);
    return ret;
}

const char *
krb5_config_vget_string (krb5_config_section *c,
			 va_list args)
{
    return krb5_config_vget (c, STRING, args);
}

krb5_boolean
krb5_config_vget_bool (krb5_config_section *c,
		       va_list args)
{
    const char *str;
    str = krb5_config_vget_string (c, args);
    if(str == NULL)
	return FALSE;
    if(strcmp(str, "yes") == 0 || strcmp(str, "true") == 0 || atoi(str))
	return TRUE;
    return FALSE;
}

krb5_boolean
krb5_config_get_bool (krb5_config_section *c,
		      ...)
{
    va_list ap;
    krb5_boolean ret;
    va_start(ap, c);
    ret = krb5_config_vget_bool(c, ap);
    va_end(ap);
    return ret;
}


#ifdef TEST

int
main(void)
{
    krb5_config_section *c;

    printf ("%d\n", krb5_config_parse_file ("/etc/krb5.conf", &c));
    print_config (stdout, c);
    printf ("[libdefaults]ticket_lifetime = %s\n",
	    krb5_config_get_string (c,
			       "libdefaults",
			       "ticket_lifetime",
			       NULL));
    printf ("[realms]foo = %s\n",
	    krb5_config_get_string (c,
			       "realms",
			       "foo",
			       NULL));
    printf ("[realms]ATHENA.MIT.EDU/v4_instance_convert/lithium = %s\n",
	    krb5_config_get_string (c,
			       "realms",
			       "ATHENA.MIT.EDU",
			       "v4_instance_convert",
			       "lithium",
			       NULL));
    return 0;
}

#endif /* TEST */
