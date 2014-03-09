#include <curl/curl.h>
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMUtilities.h"
#include "XPLMMenus.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"
#include "XPLMNavigation.h"
#include "parson.c"

#define URL_BASE "http://api.vateud.net/online/atc/"
#define URL_EXTENSION ".json"
#define REQUEST_NONE 0
#define REQUEST_ATIS 1
#define REQUEST_METAR 2

#define WINDOW_WIDTH 500
#define WINDOW_HEIGHT 200
#define WINDOW_POS_X 20
#define WINDOW_POS_Y 20 + WINDOW_HEIGHT

static XPLMWindowID	mainWindow = NULL;
static XPWidgetID icaoTextField = NULL;

static char *getData = NULL;
static int newData = 0;
static int requestType = 0;
static const char *message = NULL;
static char icaoCode[32];

void parseJSON() {
    JSON_Value *root_value = json_parse_string(getData);
    
    if (root_value != NULL) {
        JSON_Value_Type type = root_value->type;
        
        if (type == JSONArray) {
            JSON_Array *stations = json_value_get_array(root_value);
            
            char lookupCallsign[32];
            sprintf(lookupCallsign, "%s%s", icaoCode, "_ATIS");
            
            for (int i = 0; i < json_array_get_count(stations); i++) {
                const JSON_Object *station = json_array_get_object(stations, i);
                const char *callsign = json_object_dotget_string(station, "callsign");
                
                if (strcmp(callsign, lookupCallsign) == 0) {
                    message = json_object_dotget_string(station, "atis");
                    XPLMDebugString(message);
                    break;
                }
            }
        }
        
        json_value_free(root_value);
    }
    
    newData = 0;
}

void Reset() {
    //icaoCode = NULL;
    message = NULL;
    requestType = REQUEST_NONE;
    getData = NULL;
    newData = 0;
}

void MyDrawWindowCallback(
                          XPLMWindowID         inWindowID,
                          void *               inRefcon)
{
	int	left, top, right, bottom;
	float color[] = { 0.0, 0.0, 0.0 };
	
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
    
    if (getData != NULL) {
        //if (newData)
            parseJSON();
        
        if (message != NULL) {
        
            XPLMDrawTranslucentDarkBox(left, top, right, bottom);
        
            /*char out[512];
            sprintf(out, "SIZE: %d\n", (int) sizeof(message));
            XPLMDebugString(out);
        
            char m[sizeof(message)];
            strcpy(m, message[1], sizeof(message) - 1);
            m[sizeof(message) - 1] = '\0';*/
            
            char *m = strndup(message+29, 29+50);
        
            XPLMDrawString(color, left + 50, top - 20, m, NULL, xplmFont_Basic);
        
            // TODO: start thread to reset getData after x seconds and hide the window
        }
    }
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

size_t WriteData(void *ptr, size_t size, size_t nmemb, struct url_data *data) {
    size_t index = data->size;
    size_t n = (size * nmemb);
    char* tmp;
    
    data->size += (size * nmemb);
    
    tmp = (char*) realloc(data->data, data->size + 1); /* +1 for '\0' */
    
    if(tmp)
        data->data = tmp;
    else {
        if(data->data)
            free(data->data);
        
        return 0;
    }
    
    memcpy((data->data + index), ptr, n);
    data->data[data->size] = '\0';
    
    return size * nmemb;
}

char *HandleUrl(const char* url) {
    CURL *curl;
    
    struct url_data data;
    data.size = 0;
    data.data = (char*) malloc(4096);
    if(NULL == data.data)
        return NULL;
    
    data.data[0] = '\0';
    
    CURLcode res;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        res = curl_easy_perform(curl);
        
        curl_easy_cleanup(curl);
        
    }
    return data.data;
}

void *PullUrl(void *arg)
{
    if (icaoCode != NULL) {
        char url[255];
        sprintf(url, "%s%s%s", URL_BASE, icaoCode, URL_EXTENSION);
        XPLMDebugString(url);
    
        getData = HandleUrl(url);
        newData = 1;
    }
    
    return NULL;
}

int GetClosestAirportId(char *id, int index)
{    
    XPLMNavRef waypoint;
    float lat = 0.f, lon = 0.f;
    XPLMGetFMSEntryInfo(index, NULL, NULL, &waypoint, NULL, &lat, &lon);
    
    if (lat == 0.f || lon == 0.f)
        return 0;
    
    XPLMNavRef airport = XPLMFindNavAid(NULL, NULL, &lat, &lon, NULL, xplm_Nav_Airport);
    XPLMGetNavAidInfo(airport, NULL, NULL, NULL, NULL, NULL, NULL, id, NULL, NULL);
    
    return 1;
}

int GetDepartureAirportId(char *id)
{
    return GetClosestAirportId(id, 0);
}

int GetDestinationAirportId(char *id)
{
    int count = XPLMCountFMSEntries();
    if (count < 2)
        return 0;
    
    int index = count - 1;
    
    return GetClosestAirportId(id, index);
}

static int CoordInRect(float x, float y, float l, float t, float r, float b)
{
    return ((x >= l) && (x < r) && (y < t) && (y >= b));
}

int MyHandleMouseClickCallback(
                               XPLMWindowID         inWindowID,
                               int                  x,
                               int                  y,
                               XPLMMouseStatus      inMouse,
                               void *               inRefcon)
{
	static int dX = 0, dY = 0;
	static int width = 0, height = 0;
	int	left, top, right, bottom;
    
	static int dragging = 0;
    
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
    
	switch(inMouse) {
        case xplm_MouseDown:
            if (CoordInRect(x, y, left, top, right, bottom))
            {
                dX = x - left;
                dY = y - top;
                width = right - left;
                height = bottom - top;
                dragging = 1;
            }
            break;
        case xplm_MouseDrag:
            if (dragging)
            {
                left = (x - dX);
                right = left + width;
                top = (y - dY);
                bottom = top + height;
                XPLMSetWindowGeometry(inWindowID, left, top, right, bottom);
            }
            break;
        case xplm_MouseUp:
            dragging = 0;
            break;
	}
    
	return 1;
}

void MyHandleMenuCallback(void *inMenuRef, void *inItemRef)
{
    int item = (long) inItemRef;
    //char airport[32];
    
    if (item == 0 || item == 2)
        GetDepartureAirportId(icaoCode);
    else
        GetDestinationAirportId(icaoCode);
    
    //icaoCode = airport;
    XPLMDebugString(icaoCode);
    
    if (item == 0 || item == 1)
        requestType = REQUEST_ATIS;
    else
        requestType = REQUEST_METAR;
    
    pthread_t tid;
    pthread_create(&tid, NULL, PullUrl, NULL);
    
    XPShowWidget(mainWindow);
}

PLUGIN_API int XPluginStart(
                            char *		outName,
                            char *		outSig,
                            char *		outDesc)
{
	strcpy(outName, "ATIS Printer");
	strcpy(outSig, "de.bwravencl.atis_printer");
	strcpy(outDesc, "A plugin that prints ATIS data from the VATSIM network.");
	
	XPLMMenuID menu = XPLMCreateMenu("ACARS Weather", NULL, 0, MyHandleMenuCallback, 0);
    
	XPLMAppendMenuItem(menu, "Departure ATIS", (void *) 0, 1);
	XPLMAppendMenuItem(menu, "Destination ATIS", (void *) 1, 1);
    XPLMAppendMenuItem(menu, "Departure METAR", (void *) 2, 1);
	XPLMAppendMenuItem(menu, "Destination METAR", (void *) 3, 1);
    
	mainWindow = XPLMCreateWindow(
                              WINDOW_POS_X, WINDOW_POS_Y, WINDOW_POS_X + WINDOW_WIDTH, WINDOW_POS_Y - WINDOW_HEIGHT,
                              1,
                              MyDrawWindowCallback,
                              MyHandleKeyCallback,
                              MyHandleMouseClickCallback,
                              NULL);
    
	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
	XPLMDestroyWindow(mainWindow);
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
