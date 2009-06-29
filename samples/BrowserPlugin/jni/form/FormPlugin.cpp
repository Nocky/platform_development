/*
 * Copyright 2008, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "FormPlugin.h"

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <string.h>

extern NPNetscapeFuncs*         browser;
extern ANPLogInterfaceV0        gLogI;
extern ANPCanvasInterfaceV0     gCanvasI;
extern ANPPaintInterfaceV0      gPaintI;
extern ANPTypefaceInterfaceV0   gTypefaceI;
extern ANPWindowInterfaceV0     gWindowI;


static void inval(NPP instance) {
    browser->invalidaterect(instance, NULL);
}

static uint16 rnd16(float x, int inset) {
    int ix = (int)roundf(x) + inset;
    if (ix < 0) {
        ix = 0;
    }
    return static_cast<uint16>(ix);
}

static void inval(NPP instance, const ANPRectF& r, bool doAA) {
    const int inset = doAA ? -1 : 0;

    PluginObject *obj = reinterpret_cast<PluginObject*>(instance->pdata);
    NPRect inval;
    inval.left = rnd16(r.left, inset);
    inval.top = rnd16(r.top, inset);
    inval.right = rnd16(r.right, -inset);
    inval.bottom = rnd16(r.bottom, -inset);
    browser->invalidaterect(instance, &inval);
}

static void drawPlugin(SubPlugin* plugin, const ANPBitmap& bitmap, const ANPRectI& clip) {
    ANPCanvas* canvas = gCanvasI.newCanvas(&bitmap);

    ANPRectF clipR;
    clipR.left = clip.left;
    clipR.top = clip.top;
    clipR.right = clip.right;
    clipR.bottom = clip.bottom;
    gCanvasI.clipRect(canvas, &clipR);

    plugin->draw(canvas);
    gCanvasI.deleteCanvas(canvas);
}

///////////////////////////////////////////////////////////////////////////////

FormPlugin::FormPlugin(NPP inst) : SubPlugin(inst) {

    m_activeInput = NULL;

    memset(&m_usernameInput, 0, sizeof(m_usernameInput));
    memset(&m_passwordInput, 0, sizeof(m_passwordInput));

    m_usernameInput.text[0] = '\0';
    m_usernameInput.charPtr = 0;

    m_passwordInput.text[0] = '\0';
    m_passwordInput.charPtr = 0;

    m_paintInput = gPaintI.newPaint();
    gPaintI.setFlags(m_paintInput, gPaintI.getFlags(m_paintInput) | kAntiAlias_ANPPaintFlag);
    gPaintI.setColor(m_paintInput, 0xFFFFFFFF);

    m_paintActive = gPaintI.newPaint();
    gPaintI.setFlags(m_paintActive, gPaintI.getFlags(m_paintActive) | kAntiAlias_ANPPaintFlag);
    gPaintI.setColor(m_paintActive, 0xFFFFFF00);

    m_paintText = gPaintI.newPaint();
    gPaintI.setFlags(m_paintText, gPaintI.getFlags(m_paintText) | kAntiAlias_ANPPaintFlag);
    gPaintI.setColor(m_paintText, 0xFF000000);
    gPaintI.setTextSize(m_paintText, 18);

    ANPTypeface* tf = gTypefaceI.createFromName("serif", kItalic_ANPTypefaceStyle);
    gPaintI.setTypeface(m_paintText, tf);
    gTypefaceI.unref(tf);

    //register for key and visibleRect events
    ANPEventFlags flags = kKey_ANPEventFlag | kVisibleRect_ANPEventFlag;
    NPError err = browser->setvalue(inst, kAcceptEvents_ANPSetValue, &flags);
    if (err != NPERR_NO_ERROR) {
        gLogI.log(inst, kError_ANPLogType, "Error selecting input events.");
    }
}

FormPlugin::~FormPlugin() {
    gPaintI.deletePaint(m_paintInput);
    gPaintI.deletePaint(m_paintActive);
    gPaintI.deletePaint(m_paintText);
}

void FormPlugin::draw(ANPCanvas* canvas) {
    NPP instance = this->inst();
    PluginObject *obj = (PluginObject*) instance->pdata;

    const float inputWidth = 60;
    const float inputHeight = 30;
    const int W = obj->window->width;
    const int H = obj->window->height;

    // color the plugin canvas
    gCanvasI.drawColor(canvas, 0xFFCDCDCD);

    // draw the username box (5 px from the top edge)
    inval(instance, m_usernameInput.rect, true);
    m_usernameInput.rect.left = 5;
    m_usernameInput.rect.top = 5;
    m_usernameInput.rect.right = W - 5;
    m_usernameInput.rect.bottom = m_usernameInput.rect.top + inputHeight;
    gCanvasI.drawRect(canvas, &m_usernameInput.rect, getPaint(&m_usernameInput));
    drawText(canvas, m_usernameInput);
    inval(instance, m_usernameInput.rect, true);

    // draw the password box (5 px from the bottom edge)
    inval(instance, m_passwordInput.rect, true);
    m_passwordInput.rect.left = 5;
    m_passwordInput.rect.top = H - (inputHeight + 5);
    m_passwordInput.rect.right = W - 5;
    m_passwordInput.rect.bottom = m_passwordInput.rect.top + inputHeight;
    gCanvasI.drawRect(canvas, &m_passwordInput.rect, getPaint(&m_passwordInput));
    drawPassword(canvas, m_passwordInput);
    inval(instance, m_passwordInput.rect, true);
}

ANPPaint* FormPlugin::getPaint(TextInput* input) {
    return (input == m_activeInput) ? m_paintActive : m_paintInput;
}

void FormPlugin::drawText(ANPCanvas* canvas, TextInput textInput) {

    // get font metrics
    ANPFontMetrics fontMetrics;
    gPaintI.getFontMetrics(m_paintText, &fontMetrics);

    gCanvasI.drawText(canvas, textInput.text, textInput.charPtr,
                      textInput.rect.left + 5,
                      textInput.rect.bottom - fontMetrics.fBottom, m_paintText);
}

void FormPlugin::drawPassword(ANPCanvas* canvas, TextInput passwordInput) {

    //TODO draw circles instead of the actual text
}

int16 FormPlugin::handleEvent(const ANPEvent* evt) {
    NPP instance = this->inst();

    switch (evt->eventType) {
        case kDraw_ANPEventType:
            switch (evt->data.draw.model) {
                case kBitmap_ANPDrawingModel:
                    drawPlugin(this, evt->data.draw.data.bitmap, evt->data.draw.clip);
                    return 1;
                default:
                    break;   // unknown drawing model
            }
            break;

        case kLifecycle_ANPEventType:
            if (evt->data.lifecycle.action == kLooseFocus_ANPLifecycleAction) {
                if (m_activeInput) {

                    // hide the keyboard
                    //gWindowI.showKeyboard(instance, false);
                    gLogI.log(instance, kDebug_ANPLogType, "----%p Hiding Keyboard2", instance);

                    //inval the plugin
                    m_activeInput = NULL;
                    inval(instance);

                }
                return 1;
            }
            break;

        case kMouse_ANPEventType: {

            int x = evt->data.mouse.x;
            int y = evt->data.mouse.y;
            if (kDown_ANPMouseAction == evt->data.mouse.action) {

                TextInput* currentInput = validTap(x,y);

                if (currentInput)
                    gWindowI.showKeyboard(instance, true);
                else if (m_activeInput) {
                    gWindowI.showKeyboard(instance, false);
                    gLogI.log(instance, kDebug_ANPLogType, "----%p Hiding Keyboard", instance);
                }


                if (currentInput == m_activeInput)
                    return 1;

                if (m_activeInput)
                    inval(instance, m_activeInput->rect, true); // inval the old

                m_activeInput = currentInput; // set the new active input
                if (m_activeInput)
                    inval(instance, m_activeInput->rect, true); // inval the new

                return 1;
            }
            break;
        }

        case kKey_ANPEventType:
            if (evt->data.key.action == kDown_ANPKeyAction) {

                //handle navigation keys
                if (evt->data.key.nativeCode >= kDpadUp_ANPKeyCode
                        && evt->data.key.nativeCode <= kDpadCenter_ANPKeyCode) {
                    gLogI.log(instance, kDebug_ANPLogType, "----%p Recvd Nav Key", instance);
                    return 0;
                }

                if (m_activeInput) {
                    handleTextInput(m_activeInput, evt->data.key.nativeCode,
                                    evt->data.key.unichar);
                    inval(instance, m_activeInput->rect, true);
                }
                return 1;
            }
            break;

        case kVisibleRect_ANPEventType: {

            int oldScreenW = m_visibleRect.right - m_visibleRect.left;
            int oldScreenH = m_visibleRect.bottom - m_visibleRect.top;

            m_visibleRect = evt->data.visibleRect.rect;

            int newScreenW = m_visibleRect.right - m_visibleRect.left;
            int newScreenH = m_visibleRect.bottom - m_visibleRect.top;

            if (m_activeInput && (oldScreenW != newScreenW || oldScreenH != newScreenH))
                scrollIntoView(m_activeInput);

            return 1;
        }

        default:
            break;
    }
    return 0;   // unknown or unhandled event
}

void FormPlugin::handleTextInput(TextInput* input, ANPKeyCode keyCode, int32_t unichar) {
    NPP instance = this->inst();

    //make sure the input field is in view
    scrollIntoView(input);

    //handle the delete operation
    if (keyCode == kDel_ANPKeyCode) {
        if (input->charPtr > 0) {
            input->charPtr--;
        }
        return;
    }

    //check to see that the input is not full
    if (input->charPtr >= (sizeof(input->text) - 1))
        return;

    //add the character
    input->text[input->charPtr] = static_cast<char>(unichar);
    input->charPtr++;

    gLogI.log(instance, kDebug_ANPLogType, "----%p Text:  %c", instance, unichar);
}

void FormPlugin::scrollIntoView(TextInput* input) {
    NPP instance = this->inst();
    PluginObject *obj = (PluginObject*) instance->pdata;
    NPWindow *window = obj->window;

    // find the textInput's global rect coordinates
    ANPRectI inputRect;
    inputRect.left = window->x + input->rect.left;
    inputRect.top = window->y + input->rect.top;
    inputRect.right = inputRect.left + (input->rect.right - input->rect.left);
    inputRect.bottom = inputRect.top + (input->rect.bottom - input->rect.top);

    gLogI.log(instance, kDebug_ANPLogType, "----%p Checking Rect Visibility", instance);

    // if the rect is contained within visible window then do nothing
    if (inputRect.left > m_visibleRect.left
            && inputRect.right < m_visibleRect.right
            && inputRect.top > m_visibleRect.top
            && inputRect.bottom < m_visibleRect.bottom) {
        return;
    }

    // find the global (x,y) coordinates for center of the textInput
    int inputCenterX = inputRect.left + ((inputRect.right - inputRect.left)/2);
    int inputCenterY = inputRect.top + ((inputRect.bottom - inputRect.top)/2);

    gLogI.log(instance, kDebug_ANPLogType, "---- %p Input Center: %d,%d : %d,%d",
              instance, inputCenterX, inputCenterY, window->x, window->y);

    //find global (x,y) coordinates for center of the visible screen
    int screenCenterX = m_visibleRect.left + ((m_visibleRect.right - m_visibleRect.left)/2);
    int screenCenterY = m_visibleRect.top + ((m_visibleRect.bottom - m_visibleRect.top)/2);

    gLogI.log(instance, kDebug_ANPLogType, "---- %p Screen Center: %d,%d : %d,%d",
              instance, screenCenterX, screenCenterY, m_visibleRect.left, m_visibleRect.top);

    //compute the delta of the two coordinates
    int deltaX = inputCenterX - screenCenterX;
    int deltaY = inputCenterY - screenCenterY;

    gLogI.log(instance, kDebug_ANPLogType, "---- %p Centering: %d,%d : %d,%d",
              instance, deltaX, deltaY, m_visibleRect.left + deltaX, m_visibleRect.top + deltaY);

    //move the visible screen
    gWindowI.scrollTo(instance, m_visibleRect.left + deltaX, m_visibleRect.top + deltaY);

}

TextInput* FormPlugin::validTap(int x, int y) {

    if (x > m_usernameInput.rect.left && x < m_usernameInput.rect.right &&
        y > m_usernameInput.rect.top && y < m_usernameInput.rect.bottom)
        return &m_usernameInput;
    else if (x >m_passwordInput.rect.left && x < m_passwordInput.rect.right &&
             y > m_passwordInput.rect.top && y < m_passwordInput.rect.bottom)
        return &m_passwordInput;
    else
        return NULL;
}
