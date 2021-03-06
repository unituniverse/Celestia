// overlay.cpp
//
// Copyright (C) 2001, Chris Laurel <claurel@shatters.net>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

#include <cstring>
#include <cstdarg>
#include <iostream>
#include <GL/glew.h>
#include <Eigen/Core>
#include <celutil/debug.h>
#include <celutil/utf8.h>
#include <celmath/geomutil.h>
#include "vecgl.h"
#include "overlay.h"
#include "rectangle.h"
#include "render.h"
#include "texture.h"
#if NO_TTF
#include <celtxf/texturefont.h>
#else
#include <celttf/truetypefont.h>
#endif

using namespace std;
using namespace Eigen;
using namespace celmath;

Overlay::Overlay(const Renderer& r) :
    ostream(&sbuf),
    renderer(r)
{
    sbuf.setOverlay(this);
}

void Overlay::begin()
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrix(Ortho2D(0.0f, (float)windowWidth, 0.0f, (float)windowHeight));
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(0.125f, 0.125f, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    useTexture = false;
}

void Overlay::end()
{
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}


void Overlay::setWindowSize(int w, int h)
{
    windowWidth = w;
    windowHeight = h;
}

void Overlay::setFont(TextureFont* f)
{
    if (f != font)
    {
        font = f;
        fontChanged = true;
    }
}


void Overlay::beginText()
{
    glPushMatrix();
    textBlock++;
    if (font != nullptr)
    {
        font->bind();
        useTexture = true;
        fontChanged = false;
    }
}

void Overlay::endText()
{
    if (textBlock > 0)
    {
        textBlock--;
        xoffset = 0.0f;
        glPopMatrix();
    }
    font->unbind();
}


void Overlay::print(wchar_t c)
{
    if (font != nullptr)
    {
        if (!useTexture || fontChanged)
        {
            font->bind();
            useTexture = true;
            fontChanged = false;
        }

        switch (c)
        {
        case '\n':
            if (textBlock > 0)
            {
                glPopMatrix();
                glTranslatef(0.0f, (float) -(1 + font->getHeight()), 0.0f);
                xoffset = 0.0f;
                glPushMatrix();
            }
            break;
        default:
            font->render(c, xoffset, yoffset);
            xoffset += font->getAdvance(c);
            break;
        }
    }
}


void Overlay::print(char c)
{
    if (font != nullptr)
    {
        if (!useTexture || fontChanged)
        {
            font->bind();
            useTexture = true;
            fontChanged = false;
        }

        switch (c)
        {
        case '\n':
            if (textBlock > 0)
            {
                glPopMatrix();
                glTranslatef(0.0f, (float) -(1 + font->getHeight()), 0.0f);
                xoffset = 0.0f;
                glPushMatrix();
            }
            break;
        default:
            font->render(c, xoffset, yoffset);
            xoffset += font->getAdvance(c);
            break;
        }
    }
}

void Overlay::print(const char* s)
{
    int length = strlen(s);
    bool validChar = true;
    int i = 0;
    while (i < length && validChar)
    {
        wchar_t ch = 0;
        validChar = UTF8Decode(s, i, length, ch);
        i += UTF8EncodedSize(ch);
        print(ch);
    }
}

void Overlay::drawRectangle(const Rect& r)
{
    if (useTexture && r.tex == nullptr)
    {
        glBindTexture(GL_TEXTURE_2D, 0);
        useTexture = false;
    }

    renderer.drawRectangle(r);
}

void Overlay::setColor(float r, float g, float b, float a) const
{
    glColor4f(r, g, b, a);
}

void Overlay::setColor(const Color& c) const
{
    glColor4f(c.red(), c.green(), c.blue(), c.alpha());
}

void Overlay::moveBy(float dx, float dy, float dz) const
{
    glTranslatef(dx, dy, dz);
}

//
// OverlayStreamBuf implementation
//
OverlayStreamBuf::OverlayStreamBuf()
{
    setbuf(nullptr, 0);
};


void OverlayStreamBuf::setOverlay(Overlay* o)
{
    overlay = o;
}


int OverlayStreamBuf::overflow(int c)
{
    if (overlay != nullptr)
    {
        switch (decodeState)
        {
        case UTF8DecodeStart:
            if (c < 0x80)
            {
                // Just a normal 7-bit character
                overlay->print((char) c);
            }
            else
            {
                unsigned int count;

                if ((c & 0xe0) == 0xc0)
                    count = 2;
                else if ((c & 0xf0) == 0xe0)
                    count = 3;
                else if ((c & 0xf8) == 0xf0)
                    count = 4;
                else if ((c & 0xfc) == 0xf8)
                    count = 5;
                else if ((c & 0xfe) == 0xfc)
                    count = 6;
                else
                    count = 1; // Invalid byte

                if (count > 1)
                {
                    unsigned int mask = (1 << (7 - count)) - 1;
                    decodeShift = (count - 1) * 6;
                    decodedChar = (c & mask) << decodeShift;
                    decodeState = UTF8DecodeMultibyte;
                }
                else
                {
                    // If the character isn't valid multibyte sequence head,
                    // silently skip it by leaving the decoder state alone.
                }
            }
            break;

        case UTF8DecodeMultibyte:
            if ((c & 0xc0) == 0x80)
            {
                // We have a valid non-head byte in the sequence
                decodeShift -= 6;
                decodedChar |= (c & 0x3f) << decodeShift;
                if (decodeShift == 0)
                {
                    overlay->print(decodedChar);
                    decodeState = UTF8DecodeStart;
                }
            }
            else
            {
                // Bad byte in UTF-8 encoded sequence; we'll silently ignore
                // it and reset the state of the UTF-8 decoder.
                decodeState = UTF8DecodeStart;
            }
            break;
        }
    }

    return c;
}
