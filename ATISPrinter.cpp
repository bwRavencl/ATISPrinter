#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMUtilities.h"
#include "parson.c"

static XPLMWindowID	gWindow = NULL;
const static char *baseURL = "http://api.vateud.net/online/atc/";
const static char *extension = ".json";
const static char *icaoCode = "egkk";
static char *getData = NULL;

static void MyDrawWindowCallback(
                                   XPLMWindowID         inWindowID,    
                                   void *               inRefcon);    

static void MyHandleKeyCallback(
                                   XPLMWindowID         inWindowID,    
                                   char                 inKey,    
                                   XPLMKeyFlags         inFlags,    
                                   char                 inVirtualKey,    
                                   void *               inRefcon,    
                                   int                  losingFocus);    

static int MyHandleMouseClickCallback(
                                   XPLMWindowID         inWindowID,    
                                   int                  x,    
                                   int                  y,    
                                   XPLMMouseStatus      inMouse,    
                                   void *               inRefcon);

PLUGIN_API int XPluginStart(
						char *		outName,
						char *		outSig,
						char *		outDesc)
{
	strcpy(outName, "ATIS Printer");
	strcpy(outSig, "de.bwravencl.atis_printer");
	strcpy(outDesc, "A plugin that prints ATIS data from the VATSIM network.");

	/* Now we create a window.  We pass in a rectangle in left, top,
	 * right, bottom screen coordinates.  We pass in three callbacks. */

	gWindow = XPLMCreateWindow(
					50, 600, 300, 200,			/* Area of the window. */
					1,							/* Start visible. */
					MyDrawWindowCallback,		/* Callbacks */
					MyHandleKeyCallback,
					MyHandleMouseClickCallback,
					NULL);						/* Refcon - not used. */
	 
	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
	XPLMDestroyWindow(gWindow);
}

PLUGIN_API void XPluginDisable(void)
{
}

PLUGIN_API int XPluginEnable(void)
{
	return 1;
}

PLUGIN_API void XPluginReceiveMessage(
					XPLMPluginID	inFromWho,
					int				inMessage,
					void *			inParam)
{
}

/*
 * MyDrawingWindowCallback
 * 
 * This callback does the work of drawing our window once per sim cycle each time
 * it is needed.  It dynamically changes the text depending on the saved mouse
 * status.  Note that we don't have to tell X-Plane to redraw us when our text
 * changes; we are redrawn by the sim continuously.
 * 
 */
void MyDrawWindowCallback(
                                   XPLMWindowID         inWindowID,    
                                   void *               inRefcon)
{
	int		left, top, right, bottom;
	float	color[] = { 1.0, 1.0, 1.0 }; 	/* RGB White */
	
	/* First we get the location of the window passed in to us. */
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
	
	/* We now use an XPLMGraphics routine to draw a translucent dark
	 * rectangle that is our window's shape. */
	XPLMDrawTranslucentDarkBox(left, top, right, bottom);

	/* Finally we draw the text into the window, also using XPLMGraphics
	 * routines.  The NULL indicates no word wrapping. */
    
    const char *atis = "Not available.";
    if (getData != NULL) {
        JSON_Value *root_value = json_parse_string(getData);

        if (root_value != NULL) {
            JSON_Value_Type type = root_value->type;
            
            if (type == JSONArray) {
                JSON_Array *stations = json_value_get_array(root_value);
                
                char uppercaseICAO[sizeof(icaoCode)];
                int i = 0;
                while (icaoCode[i])
                {
                    char c = icaoCode[i];
                    uppercaseICAO[i] = toupper(c);
                    i++;
                }
                
                char lookupCallsign[16];
                sprintf(lookupCallsign, "%s%s", uppercaseICAO, "_ATIS");
                
                for (int i = 0; i < json_array_get_count(stations); i++) {
                    const JSON_Object *station = json_array_get_object(stations, i);
                    const char *callsign = json_object_dotget_string(station, "callsign");
                    
                    if (strcmp(callsign, lookupCallsign) == 0) {
                        atis = json_object_dotget_string(station, "atis");
                        XPLMDebugString("ATIS: ");
                        XPLMDebugString(atis);
                        XPLMDebugString("\n");
                        break;
                    }
                }
            }
            
            json_value_free(root_value);
        }
    }
    
	XPLMDrawString(color, left + 5, top - 20, 
		(char*) atis, NULL, xplmFont_Basic);
		
}                                   

void MyHandleKeyCallback(
                                   XPLMWindowID         inWindowID,    
                                   char                 inKey,    
                                   XPLMKeyFlags         inFlags,    
                                   char                 inVirtualKey,    
                                   void *               inRefcon,    
                                   int                  losingFocus)
{
}                                   

struct url_data {
    size_t size;
    char* data;
};

size_t write_data(void *ptr, size_t size, size_t nmemb, struct url_data *data) {
    size_t index = data->size;
    size_t n = (size * nmemb);
    char* tmp;
    
    data->size += (size * nmemb);
    
#ifdef DEBUG
    fprintf(stderr, "data at %p size=%ld nmemb=%ld\n", ptr, size, nmemb);
#endif
    tmp = (char*) realloc(data->data, data->size + 1); /* +1 for '\0' */
    
    if(tmp) {
        data->data = tmp;
    } else {
        if(data->data) {
            free(data->data);
        }
        fprintf(stderr, "Failed to allocate memory.\n");
        return 0;
    }
    
    memcpy((data->data + index), ptr, n);
    data->data[data->size] = '\0';
    
    return size * nmemb;
}

char *handle_url(const char* url) {
    CURL *curl;
    
    struct url_data data;
    data.size = 0;
    data.data = (char*) malloc(4096); /* reasonable size initial buffer */
    if(NULL == data.data) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return NULL;
    }
    
    data.data[0] = '\0';
    
    CURLcode res;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
        }
        
        curl_easy_cleanup(curl);
        
    }
    return data.data;
}

static void *pullURL(void *arg)
{
    char url[255];
    sprintf(url, "%s%s%s", baseURL, icaoCode, extension);
    
    getData = handle_url(url);
    
    return NULL;
}

int MyHandleMouseClickCallback(
                                   XPLMWindowID         inWindowID,    
                                   int                  x,    
                                   int                  y,    
                                   XPLMMouseStatus      inMouse,    
                                   void *               inRefcon)
{
    pthread_t tid;
    int error = pthread_create(&tid, NULL, pullURL, NULL);
	return 1;
}                                      
