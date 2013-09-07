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
#include "AglShaderProgram.h"
#include "AglSurfacePNT.h"
#include "AglTextureUbyte.h"
#include <OpenEXR/ImathMatrixAlgo.h>
#include <assert.h>

    
class LuminanceHeightFieldVertexShader::Imp
{
public:
    Imp() : defaultTextureWrapS(GL_CLAMP_TO_EDGE),
        defaultTextureWrapT(GL_CLAMP_TO_EDGE),
        texelWidthSUniform(-1), texelWidthTUniform(-1),
        heightScale(1/3.0f), heightScaleUniform(-1) {}
    static const char*  text;
    GLint               defaultTextureWrapS;
    GLint               defaultTextureWrapT;
    GLint               texelWidthSUniform;
    GLint               texelWidthTUniform;
    GLfloat             heightScale;
    GLint               heightScaleUniform;
};

const char* LuminanceHeightFieldVertexShader::Imp::text =
    "#version 150\n"
    "uniform mat4 modelViewProjMatrix;\n"
    "uniform mat3 normalMatrix;\n"
    "// The texture to use when computing the luminance-based height.\n"
    "uniform sampler2D tex;\n"
    "// The width and height of a texel in surface units.\n"
    "uniform float texelWidthS;\n"
    "uniform float texelWidthT;\n"
    "// An overall scaling factor for the luminance-based height.\n"
    "uniform float heightScale;\n"
    "in vec4 in_position;\n"
    "in vec2 in_texCoord;\n"
    "in vec3 in_normal;\n"
    "out vec2 vs_texCoord;\n"
    "out vec3 vs_normal;\n"
    "void main()\n"
    "{\n"
    "    vec4 t = texture(tex, in_texCoord);\n"
    "    // Compute height, h, as the luminance from the texture at this vertex.\n"
    "    float h = 0.2126 * t.r + 0.7152 * t.g + 0.0722 * t.b;\n"
    "    // For the normal, compute the heights using the adjacent texels.\n"
    "    vec4 tdx = textureOffset(tex, in_texCoord, ivec2(1, 0));\n"
    "    float hdx = 0.2126 * tdx.r + 0.7152 * tdx.g + 0.0722 * tdx.b;\n"
    "    vec4 tdy = textureOffset(tex, in_texCoord, ivec2(0, 1));\n"
    "    float hdy = 0.2126 * tdy.r + 0.7152 * tdy.g + 0.0722 * tdy.b;\n"
    "    // Compute a weight, w, that drops to 0 at the edges of the surface.\n"
    "    float w = min(in_texCoord.s / 0.1, 1.0);\n"
    "    w *= min((1.0 - in_texCoord.s) / 0.1, 1.0);\n"
    "    w *= min(in_texCoord.t / 0.1, 1.0);\n"
    "    w *= min((1.0 - in_texCoord.t) / 0.1, 1.0);\n"
    "    // Include an overall scaling for the height.\n"
    "    w *= heightScale;\n"
    "    h *= w;\n"
    "    hdx *= w;\n"
    "    hdy *= w;\n"
    "    vec4 v = in_position;\n"
    "    v.z += h;\n"
    "    gl_Position = modelViewProjMatrix * v;\n"
    "    vs_texCoord = in_texCoord;\n"
    "    // We cannot know exactly how far the adjacent pixels are in X and Y, so use an\n"
    "    // approximation of the texel width and height in surface units.\n"
    "    vec3 n = cross(vec3(texelWidthS, 0, hdx - h), vec3(0, texelWidthT, hdy - h));\n"
    "    // VertexShaderPNT expects in_normal to be used, even though\n"
    "    // this shader is unusual in that it does not need it.\n"
    "    vs_normal = in_normal;\n"
    "    vs_normal = normalize(normalMatrix * n);\n"
    "}\n";

LuminanceHeightFieldVertexShader::LuminanceHeightFieldVertexShader() :
    Agl::VertexShaderPNT(Imp::text), _m(new Imp)
{
}

LuminanceHeightFieldVertexShader::~LuminanceHeightFieldVertexShader()
{
}

void LuminanceHeightFieldVertexShader::setHeightScale(GLfloat s)
{
    _m->heightScale = s;
}

GLfloat LuminanceHeightFieldVertexShader::heightScale() const
{
    return _m->heightScale;
}

void LuminanceHeightFieldVertexShader::postLink()
{
    VertexShaderPNT::postLink();

    _m->texelWidthTUniform = glGetUniformLocation(shaderProgram()->id(), "texelWidthT");
    _m->texelWidthSUniform = glGetUniformLocation(shaderProgram()->id(), "texelWidthS");
    _m->heightScaleUniform = glGetUniformLocation(shaderProgram()->id(), "heightScale");
    
    // Use assertions rather than exceptions here because the shader text
    // not set by the caller.
    
    assert (_m->texelWidthSUniform >= 0);
    assert (_m->texelWidthTUniform >= 0);
    assert (_m->heightScaleUniform >= 0);
}

void LuminanceHeightFieldVertexShader::preDraw()
{
    // Save the current texture wrapping settings so they can be changed and then
    // restored after drawing.
    
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &_m->defaultTextureWrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &_m->defaultTextureWrapT);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glUniform1f(_m->heightScaleUniform, _m->heightScale);
}

void LuminanceHeightFieldVertexShader::preDraw(Agl::SurfacePNT* surface)
{
    Agl::VertexShaderPNT::preDraw(surface);
    
    if (Agl::TextureUbyte* texture = surface->texture())
    {
        texture->bind();
        
        // When the shader computes the normal vector at each warped vertex,
        // it takes the cross product of vectors to nearby locations in the
        // height field.  To efficiently get the heights at those locations,
        // it uses adjacent texels.  It also needs the displacements in x and
        // y to those locations, and it approximates those displacements as
        // the width and height of a texel in surface units.  Getting those
        // units correct requires knowing the scaling that was applied to the
        // surface.
        
        Imath::M44f modelMatrix = surface->modelMatrix();
        Imath::V3f scale;
        Imath::extractScaling(modelMatrix, scale);
                
        GLfloat texelWidthS = scale.x / texture->width();
        GLfloat texelWidthT = scale.y / texture->height();
        
        glUniform1f(_m->texelWidthSUniform, texelWidthS);
        glUniform1f(_m->texelWidthTUniform, texelWidthT);
    }
}

void LuminanceHeightFieldVertexShader::postDraw()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, _m->defaultTextureWrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, _m->defaultTextureWrapT);
}

const char* LuminanceHeightFieldVertexShader::modelViewProjectionMatrixUniformName() const
{
    return "modelViewProjMatrix";
}

const char* LuminanceHeightFieldVertexShader::normalMatrixUniformName() const
{
    return "normalMatrix";
}

const char* LuminanceHeightFieldVertexShader::positionAttributeName() const
{
    return "in_position";
}

const char* LuminanceHeightFieldVertexShader::normalAttributeName() const
{
    return "in_normal";
}

const char* LuminanceHeightFieldVertexShader::texCoordAttributeName() const
{
    return "in_texCoord";
}


