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
//  FacetiousInit.cpp
//

#include "FacetiousCppNSOpenGL.h"

Aoc::CppNSOpenGLBase* facetiousCreator(Aoc::CppNSOpenGLRequester* req)
{
    return new FacetiousCppNSOpenGL(req);
}

// This function must be labeled 'extern "C"' so it can be called as pure C
// from AppDelegate.m.

extern "C" void facetiousInit()
{
    // Tell Aoc::CppNSOpenGLBase to call facetiousCreator() when the Objective-C++
    // class AocNSOpenGLView requests the creation of an instance of a class
    // derived from Aoc::CppNSOpenGLBase.
    
    Aoc::CppNSOpenGLBase::setCreator(facetiousCreator);
}