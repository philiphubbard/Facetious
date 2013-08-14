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
//  FacetiousShader.cpp
//

#include "FacetiousShader.h"
#include "Agl/AglShaderProgram.h"
#include "Agl/AglSurfacePNT.h"
#include "Agl/AglTextureUbyte.h"
#include <OpenEXR/ImathMatrixAlgo.h>
#include <assert.h>

    
class IntensityHeightFieldVertexShader::Imp
{
public:
    static const char*  text;
    GLint               defaultTextureWrapS;
    GLint               defaultTextureWrapT;
    GLint               texelWidthSUniform;
    GLint               texelWidthTUniform;
};

const char* IntensityHeightFieldVertexShader::Imp::text =
    "#version 150\n"
    "uniform mat4 modelViewProjMatrix;\n"
    "uniform mat3 normalMatrix;\n"
    "uniform sampler2D tex;\n"
    "uniform float texelWidthS;\n"
    "uniform float texelWidthT;\n"
    "in vec4 in_position;\n"
    "in vec2 in_texCoord;\n"
    "in vec3 in_normal;\n"
    "out vec2 vs_texCoord;\n"
    "out vec3 vs_normal;\n"
    "void main()\n"
    "{\n"
    "    vec4 t = texture(tex, in_texCoord);\n"
    "    float h = 0.2126 * t.r + 0.7152 * t.g + 0.0722 * t.b;\n"
    "    vec4 tdx = textureOffset(tex, in_texCoord, ivec2(1, 0));\n"
    "    float hdx = 0.2126 * tdx.r + 0.7152 * tdx.g + 0.0722 * tdx.b;\n"
    "    vec4 tdy = textureOffset(tex, in_texCoord, ivec2(0, 1));\n"
    "    float hdy = 0.2126 * tdy.r + 0.7152 * tdy.g + 0.0722 * tdy.b;\n"
    "    float w = min(in_texCoord.s / 0.1, 1.0);\n"
    "    w *= min((1.0 - in_texCoord.s) / 0.1, 1.0);\n"
    "    w *= min(in_texCoord.t / 0.1, 1.0);\n"
    "    w *= min((1.0 - in_texCoord.t) / 0.1, 1.0);\n"
    "    w /= 3;\n"
    "    h *= w;\n"
    "    hdx *= w;\n"
    "    hdy *= w;\n"
    "    vec4 v = in_position;\n"
    "    v.z += h;\n"
    "    gl_Position = modelViewProjMatrix * v;\n"
    "    vs_texCoord = in_texCoord;"
    "    vec3 n = cross(vec3(texelWidthS, 0, hdx - h), vec3(0, texelWidthT, hdy - h));\n"
    "    // VertexShaderPNT expects in_normal to be used, even though\n"
    "    // this shader is unusual in that it does not need it.\n"
    "    vs_normal = in_normal;\n"
    "    vs_normal = normalize(normalMatrix * n);\n"
    "}\n";

IntensityHeightFieldVertexShader::IntensityHeightFieldVertexShader() :
    Agl::VertexShaderPNT(Imp::text), _m(new Imp)
{
}

IntensityHeightFieldVertexShader::~IntensityHeightFieldVertexShader()
{
}

void IntensityHeightFieldVertexShader::postLink()
{
    VertexShaderPNT::postLink();

    _m->texelWidthTUniform = glGetUniformLocation(shaderProgram()->id(), "texelWidthT");
    _m->texelWidthSUniform = glGetUniformLocation(shaderProgram()->id(), "texelWidthS");
    
    // Use assertions rather than exceptions here because the shader text
    // not set by the caller.
    
    assert (_m->texelWidthSUniform >= 0);
    assert (_m->texelWidthTUniform >= 0);
}

void IntensityHeightFieldVertexShader::preDraw()
{
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &_m->defaultTextureWrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &_m->defaultTextureWrapT);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void IntensityHeightFieldVertexShader::preDraw(Agl::SurfacePNT* surface)
{
    Agl::VertexShaderPNT::preDraw(surface);
    
    if (Agl::TextureUbyte* texture = surface->texture())
    {
        texture->bind();
        
        Imath::M44f modelMatrix = surface->modelMatrix();
        Imath::V3f scale;
        Imath::extractScaling(modelMatrix, scale);
        
        // This shader assumes that the surface is a 1.0 x 1.0 square
        // like an Agl::FlattishRectangular surface.  Since the shader's
        // behavior is so specialized, such an assumption does not seem
        // unreasonable.
    
        GLfloat texelWidthS = scale.x / texture->width();
        GLfloat texelWidthT = scale.y / texture->height();
        
        glUniform1f(_m->texelWidthSUniform, texelWidthS);
        glUniform1f(_m->texelWidthTUniform, texelWidthT);
    }
}

void IntensityHeightFieldVertexShader::postDraw()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, _m->defaultTextureWrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, _m->defaultTextureWrapT);
}

const char* IntensityHeightFieldVertexShader::modelViewProjectionMatrixUniformName() const
{
    return "modelViewProjMatrix";
}

const char* IntensityHeightFieldVertexShader::normalMatrixUniformName() const
{
    return "normalMatrix";
}

const char* IntensityHeightFieldVertexShader::positionAttributeName() const
{
    return "in_position";
}

const char* IntensityHeightFieldVertexShader::normalAttributeName() const
{
    return "in_normal";
}

const char* IntensityHeightFieldVertexShader::texCoordAttributeName() const
{
    return "in_texCoord";
}


