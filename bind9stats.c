/*
 * bind9stats.c (C) April 2012 by JP Mens
 * Obtain statistics from the XML produced by a BIND9.x statistics-server.
 * Depending on the argv[0], this program produces different output.
 *
 * Usage:
 *	export bind9statsurl=http://127.0.0.2:8053/
 *	        			  	  ^ trailing slash
 */

#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <libgen.h>
#include <unistd.h> 
#include <libxml/parser.h> 
#include <libxml/xpath.h> 
#include <libxml/nanohttp.h> 

static struct _types {
	char *qtype;
	long count;
} *tp, types[] = {
	{"A",		0, },
	{"AAAA",	0, },
	{"AXFR",	0, },
	{"CNAME",	0, },
	{"DNAME",	0, },
	{"DNSKEY",	0, },
	{"DS",		0, },
	{"MX",		0, },
	{"NAPTR",	0, },
	{"NS",		0, },
	{"NSEC",	0, },
	{"NSEC3",	0, },
	{"PTR",		0, },
	{"RRSIG",	0, },
	{"SOA",		0, },
	{"SRV",		0, },
	{"SSHFP",	0, },
	{"TLSA",	0, },
	{"TXT",		0, },
	{ NULL,		-1 } };
long otherqtype;

/*
 * Use nano from libxml2 because it's included for free ;-)
 * Switch to libCurl if you prefer.
 */

#define MAXBUF 2048 

char *http_fetch(const char *url) 
{ 
        void *ctx; 
        int len = 0; 
        char *buf = NULL, *ct; 

        if (!(ctx = xmlNanoHTTPOpen(url, &ct))) { 
                fprintf(stderr, "Can't fetch url %s\n", url); 
        } else { 
                len = xmlNanoHTTPContentLength(ctx); 
                if(len <= 0) len = MAXBUF; 
                if ((buf = (char*)malloc(len)) == NULL) {
			xmlNanoHTTPClose(ctx); 
			return (NULL);
		}
                len = xmlNanoHTTPRead(ctx,buf,len); 
                buf[len] = 0; 
                xmlNanoHTTPClose(ctx); 
        } 
        return buf; 
} 

xmlDocPtr fetchdoc(char *url) 
{ 
        char *buf = http_fetch(url); 
        xmlDocPtr doc; 

        if (buf == NULL) 
                return (NULL); 

        doc = xmlParseMemory(buf, strlen(buf)); 
        if (doc == NULL) { 
                fprintf(stderr,"Document not parsed successfully. \n"); 
                return (NULL); 
        } 
        return (doc); 
} 

xmlXPathObjectPtr 
getnodeset(xmlDocPtr doc, xmlChar *xpath){ 

        xmlXPathContextPtr context; 
        xmlXPathObjectPtr result; 

        context = xmlXPathNewContext(doc); 
        result = xmlXPathEvalExpression(xpath, context); 
        if (xmlXPathNodeSetIsEmpty(result->nodesetval)){ 
                printf("No result\n"); 
                return NULL; 
        } 
        xmlXPathFreeContext(context); 
        return (result); 
} 

void rdtype(xmlDocPtr doc, xmlNodePtr cur, FILE *fp) 
{
        char *qtype = NULL, *counter = NULL; 
	int typefound = 0;

        while (cur != NULL) { 
                if (!strcmp(cur->name, "name")) { 
                        qtype = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1); 
                } else if (!strcmp(cur->name, "counter")) { 
                        counter = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1); 
                } 

                cur = cur->next; 
	}

	if (qtype && counter) {
		// printf("qtype=[%s] %s\n", qtype?qtype: "NIL", counter);

		for (tp = types; tp && tp->qtype; tp++) {
			if (!strcmp(tp->qtype, qtype)) {
				tp->count = atol(counter);
				typefound = 1;
				break;
			}
		}
	}

	if (!typefound && counter) {
		otherqtype += atol(counter);
	}

	if (qtype)
		xmlFree(qtype);
	if (counter)
		xmlFree(counter);
}

void querystats(xmlDocPtr doc, xmlNodePtr cur, FILE *fp) 
{ 

        while (cur != NULL) { 

                if (!strcmp(cur->name, "rdtype")) { 
                        rdtype(doc, cur->xmlChildrenNode, stdout); 
		}

                cur = cur->next; 
        } 

	for (tp = types; tp && tp->qtype; tp++) {
		printf("%s.value %ld\n", tp->qtype, tp->count);
	}
	printf("OTHER.value %ld\n", otherqtype);


} 

void querystats_config()
{
	printf(
		"graph_title BIND9 Queries by type\n"
		"graph_category BIND9\n");
	for (tp = types; tp && tp->qtype; tp++) {
		printf("%s.type COUNTER\n", tp->qtype);
		printf("%s.min 0\n", tp->qtype);
		// printf("%s.draw STACK\n", tp->qtype);
	}
	printf("OTHER.type COUNTER\n");
	printf("OTHER.min 0\n");
	// printf("OTHER.draw STACK\n");


	for (tp = types; tp && tp->qtype; tp++) {
		printf("%s.label %s\n", tp->qtype, tp->qtype);
	}
	printf("OTHER.label OTHER\n");

}

void memstats_config()
{
	printf(
		"graph_title BIND9 Memory usage\n"
		"graph_category BIND9\n"
		"totaluse.type GAUGE\n"
		"totaluse.label Total RAM usage\n"
		"inuse.label In use\n"
		 );

}

void memstats(xmlDocPtr doc, xmlNodePtr cur, FILE *fp) 
{ 
        while (cur != NULL) { 

                if (!strcmp(cur->name, "TotalUse")) { 
			printf("totaluse.value %s\n", 
				xmlNodeListGetString(doc, cur->xmlChildrenNode, 1)); 
		} else if (!strcmp(cur->name, "InUse")) { 
			printf("inuse.value %s\n", 
				xmlNodeListGetString(doc, cur->xmlChildrenNode, 1)); 
		}

                cur = cur->next; 
        } 
} 

int main(int argc, char **argv) 
{ 

        char *url, *progname, *function; 
        xmlDocPtr doc; 
        xmlChar *xpath;
        xmlNodeSetPtr nodeset; 
        xmlXPathObjectPtr result; 
        int i; 
	void (*printconf)(void);
	void (*printfunc)(xmlDocPtr doc, xmlNodePtr cur, FILE *fp);

	if ((url = getenv("bind9statsurl")) == NULL) {
		fprintf(stderr, "%s: must set bind9statsurl in environment\n", *argv);
		exit(2);
	}


	if ((progname = basename(argv[0])) == NULL) {
		fprintf(stderr, "%s: can't get basename()\n", *argv);
		exit(2);
	}

	if ((function = strrchr(progname, '_')) == NULL) {
		fprintf(stderr, "%s: argv[0] has no underscore: don't know what to do.\n", *argv);
		exit(2);
	}

	if (!strcmp(function, "_memory")) {
		xpath = (xmlChar *)"/isc/bind/statistics/memory/summary";
		printfunc = memstats;
		printconf = memstats_config;
	} else if (!strcmp(function, "_qstats")) {
		xpath = (xmlChar *)"/isc/bind/statistics/server/queries-in";
		printfunc = querystats;
		printconf = querystats_config;
	}

	if (argc > 1 && strcmp(argv[1], "config") == 0) {
		printconf();
		exit(0);
	}

        doc = fetchdoc(url); 
        result = getnodeset (doc, xpath); 
        if (result) { 
                nodeset = result->nodesetval; 
                for (i=0; i < nodeset->nodeNr; i++) { 

                        xmlNodePtr cur; 
                        cur = nodeset->nodeTab[i]; 


                        printfunc(doc, cur->xmlChildrenNode, stdout); 
                } 
                xmlXPathFreeObject (result); 
        } 
        xmlFreeDoc(doc); 
        xmlCleanupParser(); 

        return (0); 
} 
