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
// FacetiousShader.h
//
// IntensityHeightFieldVertexShader: An OpenGL vertex shader that warps
// a flat grid surface, giving each vertex a height based on the luminance
// (perceived brightness) of a texture at the position of the vertex.
//
// IntensityPhongShaderProgram, IntensityHarmonicsShaderProgram,
// BasicPhongShaderProgram, BasicHarmonicsShaderProgra: typedefs for
// instatiations of the Agl::ShaderProgramSpecific template, for
// the four combinations of vertex and fragment shaders used in the
// Facetious application.

#ifndef __FacetiousShader__
#define __FecetiousShader__

#include "Agl/AglShaderProgramSpecific.h"
#include "Agl/AglVertexShaderPNT.h"

namespace Agl
{
    class BasicVertexShader;
    class FlattishRectangularSurface;
    class PhongOneDirectionalFragmentShader;
    class SphericalHarmonicsFragmentShader;
}

class IntensityHeightFieldVertexShader : public Agl::VertexShaderPNT
{
public:
    IntensityHeightFieldVertexShader();
    virtual ~IntensityHeightFieldVertexShader();
    
    // Set and get a scale factor for the luminance-based height at each
    // vertex (1.0f leaves the height unscaled).
    
    void                setHeightScale(GLfloat);
    GLfloat             heightScale() const;
    
    // Necessary because overloading one version of postLink() implicitly
    // hides the other versions from the base class.  See Scott Meyers'
    // "Effective C++: Third Edition," Item 33.
    
    using VertexShaderPNT::postLink;
    
    // Perform initialization after the linking of the shader program with
    // this shader.
    
    virtual void        postLink();
    
    // Perform initialization before drawing any surfaces with this shader.
    
    virtual void        preDraw();
    
    // Perform initialization before drawing a particular surface with this
    // shader.
    
    virtual void        preDraw(Agl::SurfacePNT*);
    
    // Clean up after drawing all surfaces with this shader.
    
    virtual void        postDraw();
    
protected:
    
    // The names in the shader code for the uniform variables for the
    // model-view-projection matrix and the normal matrix.
    
    virtual const char* modelViewProjectionMatrixUniformName() const;
    virtual const char* normalMatrixUniformName() const;
    
    // The names in the shader code for the attribute variables for the
    // vertex position, the normal vector and the texture coordinates.
    
    virtual const char* positionAttributeName() const;
    virtual const char* normalAttributeName() const;
    virtual const char* texCoordAttributeName() const;

private:
    
    // The data members of this class are hidden in the .cpp file.
   
    class Imp;
    std::unique_ptr<Imp> _m;
};


// Typedefs for specific shader programs instantiated from the template.

typedef Agl::ShaderProgramSpecific<IntensityHeightFieldVertexShader,
                                   Agl::PhongOneDirectionalFragmentShader,
                                   Agl::FlattishRectangularSurface>
    IntensityPhongShaderProgram;

typedef Agl::ShaderProgramSpecific<IntensityHeightFieldVertexShader,
                                   Agl::SphericalHarmonicsFragmentShader,
                                   Agl::FlattishRectangularSurface>
    IntensityHarmonicsShaderProgram;

typedef Agl::ShaderProgramSpecific<Agl::BasicVertexShader,
                                   Agl::PhongOneDirectionalFragmentShader,
                                   Agl::FlattishRectangularSurface>
    BasicPhongShaderProgram;

typedef Agl::ShaderProgramSpecific<Agl::BasicVertexShader,
                                   Agl::SphericalHarmonicsFragmentShader,
                                   Agl::FlattishRectangularSurface>
    BasicHarmonicsShaderProgram;


#endif
