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
#define STATUS_EMPTY 0
#define STATUS_PAPER 1

#define WINDOW_WIDTH 200
#define WINDOW_HEIGHT 280
#define WINDOW_POS_X 20
#define WINDOW_POS_Y 20 + WINDOW_HEIGHT

#define BITMAP_PRINTER_EMPTY "/Users/matteo/printer_empty"
#define BITMAP_PRINTER_PAPER "/Users/matteo/printer_paper"
#define BITMAP_WIDTH 256
#define BITMAP_HEIGHT 256

static XPLMDataRef red = NULL, green = NULL, blue = NULL;

static XPLMWindowID	mainWindow = NULL;
static XPWidgetID icaoTextField = NULL;

static char *getData = NULL;
static int status = STATUS_EMPTY;
static int textureEmpty = 0;
static int texturePaper = 0;
const static char *icaoCode = "ebbr";

static int DrawTexture(int left, int top, int right, int bottom)
{
    if (status == STATUS_PAPER)
        XPLMBindTexture2d(texturePaper, 0);
    else
        XPLMBindTexture2d(textureEmpty, 0);
    
    XPLMSetGraphicsState(0, 1, 0, 0, 0, 0, 0);
    
    glColor3f(XPLMGetDataf(red), XPLMGetDataf(green), XPLMGetDataf(blue));
    
    glPushMatrix();
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 1.f);        glVertex2f(left, bottom);
    glTexCoord2f(0.f, 0.f);        glVertex2f(left, top);
    glTexCoord2f(1.f, 0.f);        glVertex2f(right, top);
    glTexCoord2f(1.f, 1.f);        glVertex2f(right, bottom);
    glEnd();
    glPopMatrix();
    glFlush();
}

void MyDrawWindowCallback(
                          XPLMWindowID         inWindowID,
                          void *               inRefcon)
{
	int	left, top, right, bottom;
	float color[] = { 0.0, 0.0, 0.0 };
	
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
    
    DrawTexture(left, top, right, bottom);
    
    const char *atis = "---";
    if (getData != NULL) {
        atis = "Not available.";
        
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
                        status = STATUS_PAPER;
                        //XPLMDebugString("ATIS: ");
                        //XPLMDebugString(atis);
                        //XPLMDebugString("\n");
                        break;
                    }
                }
            }
            
            json_value_free(root_value);
        }
    }
    
	XPLMDrawString(color, left + 50, top - 20,
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
    char url[255];
    sprintf(url, "%s%s%s", URL_BASE, icaoCode, URL_EXTENSION);
    
    getData = HandleUrl(url);
    
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
            if (CoordInRect(x, y, left, top, right, top - 15))
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
    
    pthread_t tid;
    pthread_create(&tid, NULL, PullUrl, NULL);
    
    status = STATUS_EMPTY;
    
    char dep[32] = "NONE";
    char dest[32] = "NONE";
    
    GetDepartureAirportId(dep);
    GetDestinationAirportId(dest);
    XPLMDebugString("Departure: ");
    XPLMDebugString(dep);
    XPLMDebugString("\n\n");
    XPLMDebugString("Destination: ");
    XPLMDebugString(dest);
    XPLMDebugString("\n\n");
    
	return 1;
}

GLuint LoadTexture(const char *filename, int width, int height)
{
    GLuint texture;
    unsigned char *data;
    FILE *file;
    
    // open texture data
    file = fopen(filename, "rb");
    if (file == NULL) return 0;
    
    // allocate buffer
    data = (unsigned char*) malloc(width * height * 4);
    
    // read texture data
    fread(data, width * height * 4, 1, file);
    fclose(file);
    
    // allocate a texture name
    glGenTextures(1, &texture);
    
    // select our current texture
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // select modulate to mix texture with color for shading
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_DECAL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_DECAL);
    
    // when texture area is small, bilinear filter the closest mipmap
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    // when texture area is large, bilinear filter the first mipmap
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // texture should tile
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    
    // build our texture mipmaps
    gluBuild2DMipmaps(GL_TEXTURE_2D, 4, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    // free buffer
    free(data);
    
    return texture;
}

PLUGIN_API int XPluginStart(
                            char *		outName,
                            char *		outSig,
                            char *		outDesc)
{
	strcpy(outName, "ATIS Printer");
	strcpy(outSig, "de.bwravencl.atis_printer");
	strcpy(outDesc, "A plugin that prints ATIS data from the VATSIM network.");
    
    red = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_r");
	green = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_g");
	blue = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_b");
    
    textureEmpty = LoadTexture(BITMAP_PRINTER_EMPTY, BITMAP_WIDTH, BITMAP_HEIGHT);
    texturePaper = LoadTexture(BITMAP_PRINTER_PAPER, BITMAP_WIDTH, BITMAP_HEIGHT);
    
	mainWindow = XPLMCreateWindow(
                              WINDOW_POS_X, WINDOW_POS_Y, WINDOW_POS_X + WINDOW_WIDTH, WINDOW_POS_Y - WINDOW_HEIGHT,
                              1,
                              MyDrawWindowCallback,
                              MyHandleKeyCallback,
                              MyHandleMouseClickCallback,
                              NULL);
    icaoTextField = XPCreateWidget(WINDOW_POS_X + 10, WINDOW_POS_Y - 10, WINDOW_POS_X + 80, WINDOW_POS_Y - 40,
                   1,
                   "TEST",
                   0,
                   mainWindow,
                   xpWidgetClass_TextField);
    XPSetWidgetProperty(icaoTextField, xpProperty_MaxCharacters, 4);
    
	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
	XPLMDestroyWindow(mainWindow);
    
    //XPLMUnregisterDrawCallback(DrawTexture, xplm_Phase_Window, 0, NULL);
    /*XPLMBindTexture2d(textureEmpty,0);
    GLuint t = textureEmpty;
    glDeleteTextures(1, &t);
    XPLMBindTexture2d(texturePaper,0);
    t = texturePaper;
    glDeleteTextures(1, &t);*/
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
