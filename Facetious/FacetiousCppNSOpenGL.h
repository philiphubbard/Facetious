// Copyright (c) 2013 Philip M. Hubbard
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// http://opensource.org/licenses/MIT

//
// FacetiousCppNSOpenGL.h
//
// FacetiousCppNSOpenGL: The main class implementing the Facetious application.
// The code in FacetiousInit.{h,cpp} causes an instance of this class to be
// created and associated with the AocCppNSOpenGLView created by the AppDelegate.
// The AocCppNSOpenGLView then calls functions on this class to handle OpenGL
// initialization, window reshaping, drawing and keyboard input.
//

#ifndef __FacetiousCppNSOpenGL__
#define __FacetiousCppNSOpenGL__

#include "Aoc/AocCppNSOpenGLBase.h"

class FacetiousCppNSOpenGL : public Aoc::CppNSOpenGLBase
{
public:
    
    // The Aoc::CppNSOpenGLRequester instance can be used to request a redraw,
    // for example after the processing of a keyboard event makes a change that
    // needs to be reflected in the rendering.
    
    FacetiousCppNSOpenGL(Aoc::CppNSOpenGLRequester*);
    virtual ~FacetiousCppNSOpenGL();
    
    // Initialize OpenGL for the Facetious application.
    
    virtual void init();
    
    // Handle the resizing of the window.
    
    virtual void reshape(int width, int height);
    
    // Perform OpenGL rendering.
    
    virtual void draw();
    
    // Handle a keyboard event.
    
    virtual void keyDown(Aoc::CppNSOpenGLBase::KeyEvent keyEvent);
    
private:
    
    // Details of the class' data are hidden in the .cpp file.
    
    class Imp;
    std::unique_ptr<Imp> _m;
};

#endif
