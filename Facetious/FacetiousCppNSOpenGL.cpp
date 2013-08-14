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
//  FacetiousCppNSOpenGL.cpp
//

#include "FacetiousCppNSOpenGL.h"
#include "FacetiousShader.h"

// Note that when using the profile for OpenGL version 3.2, it is important to use the
// OpenGL/gl3.h header instead of the OpenGL/gl.h header (and thus the versions of
// glGenVertexArray and glBindVertexArray without the APPLE extension).

#include <OpenGL/gl3.h>
#include <ImageIO/CGImageSource.h>

#include "Aoc/AocCppAVFoundationCamera.h"
#include "Aoc/AocCppCIDetector.h"

#include "Aut/AutAlert.h"
#include "Aut/AutAnim.h"
#include "Aut/AutRunningAverage.h"
#include "Agl/AglUtilities.h"
#include "Agl/AglShader.h"
#include "Agl/AglImagePool.h"
#include "Agl/AglTextureUbyte.h"
#include "Agl/AglFlattishRectangularSurface.h"
#include "Agl/AglBasicVertexShader.h"
#include "Agl/AglPhongOneDirectionalFragmentShader.h"
#include "Agl/AglSphericalHarmonicsFragmentShader.h"

#include <OpenEXR/ImathFrustum.h>
#include <OpenEXR/ImathMatrix.h>
#include <OpenEXR/ImathVec.h>

#include <thread>
#include <chrono>
#include <deque>
#include <assert.h>

class FacetiousCppNSOpenGL::Imp
{
public:
    
    Imp(Aoc::CppNSOpenGLRequester* r) : requester(r), iCurrentShaderProgram(0),
        rotAngleX(0.0f), rotAngleY(0.0f), frontSurface(0), frontTexture(0), camera(0),
        cameraImage(0), detectorThread(0), detector(0), runDetectorThread(true), detectorImage(0),
        detectorImageWidthMax(64), detectorImageWidth(0), detectorImageHeight(0),
        stabilize(true), animTimerThread(0), runAnimTimerThread(true) {}
    
    static void             textureDataFromImage(CGImageRef image,
                                                 GLubyte* data);
    
    void                    detectorThreadFunc();
    
    void                    animTimerThreadFunc();
    
    Aoc::CppNSOpenGLRequester*   requester;
    
    std::vector<Agl::Shader*>               shaders;
    std::vector<Agl::VertexShaderPNT*>      vertexShaders;
    std::vector<Agl::PhongOneDirectionalFragmentShader*> phongFragmentShaders;
    std::vector<Agl::ShaderProgram*>        frontShaderPrograms;
    std::vector<Agl::ShaderProgram*>        backShaderPrograms;
    size_t                                  iCurrentShaderProgram;
    
    int                     viewWidth;
    int                     viewHeight;
    
    float                   rotAngleX;
    float                   rotAngleY;
    
    Imath::V3f             ambientColor;
    Imath::V3f             lightColor;
    
    Agl::FlattishRectangularSurface* frontSurface;
    Agl::FlattishRectangularSurface* backSurface;
    
    Agl::TextureUbyte*               frontTexture;
    Agl::TextureUbyte*               backTexture;
    
    class Camera : public Aoc::CppAVFoundationCamera
    {
    public:
        Camera(FacetiousCppNSOpenGL::Imp* imp);
        virtual void handleCapturedImage(CGImageRef image);
        
    private:
        FacetiousCppNSOpenGL::Imp*    _appImp;
    };
    
    Camera*                 camera;
    
    std::mutex              cameraImageMutex;
    std::condition_variable cameraImageCond;
    CGImageRef              cameraImage;
    
    std::thread*            detectorThread;
    Aoc::CppCIDetector*     detector;
    
    std::mutex              runDetectorThreadMutex;
    bool                    runDetectorThread;
    
    Agl::ImagePool          detectorImagePool;
    
    std::mutex              detectorMutex;
    GLubyte*                detectorImage;
    GLsizei                 detectorImageWidthMax;
    GLsizei                 detectorImageWidth;
    GLsizei                 detectorImageHeight;
    Aoc::CppCIDetector::Face detectedFace;
    
    bool                     stabilize;

    std::thread*             animTimerThread;
    
    std::mutex               runAnimTimerThreadMutex;
    bool                     runAnimTimerThread;
    
    std::mutex               animRunningMutex;
    Aut::Anim<float>         anim;
};

namespace
{
    CGImageRef imageFromFile(const char* fileName)
    {
        CFStringRef fileString = CFStringCreateWithCString(0, fileName, kCFStringEncodingASCII);
        CFURLRef url = CFURLCreateWithFileSystemPath(0, fileString, kCFURLPOSIXPathStyle, 0);
        CGImageSourceRef source = CGImageSourceCreateWithURL(url, CFDictionaryRef());
        CGImageRef image = CGImageSourceCreateImageAtIndex (source, 0, 0);
        CFRelease(fileString);
        CFRelease(url);
        CFRelease(source);
        return image;
    }
}

//

FacetiousCppNSOpenGL::Imp::Camera::Camera(FacetiousCppNSOpenGL::Imp* imp) :
    _appImp(imp)
{
}

void FacetiousCppNSOpenGL::Imp::Camera::handleCapturedImage(CGImageRef image)
{
    {
        std::lock_guard<std::mutex> lock(_appImp->cameraImageMutex);
        if (_appImp->cameraImage)
            CGImageRelease(_appImp->cameraImage);
        _appImp->cameraImage = image;
    }
    
    if (_appImp->detectorImagePool.width() == 0)
    {
        _appImp->detectorImagePool.setSize(GLsizei(CGImageGetWidth(image)),
                                           GLsizei(CGImageGetHeight(image)), 4);
    }
    
    _appImp->cameraImageCond.notify_one();
}

//

void FacetiousCppNSOpenGL::Imp::textureDataFromImage (CGImageRef image,
                                                    GLubyte* data)
{
    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
    
    const size_t bitsPerComp = 8;
    CGContextRef context = CGBitmapContextCreate(data, width, height,
                                                 bitsPerComp, width * 4,
                                                 CGImageGetColorSpace(image),
                                                 kCGImageAlphaPremultipliedLast);
    
    // Flip the image to match OpenGL's coordinates.
    
    CGContextTranslateCTM(context, 0., height);
    CGContextScaleCTM(context, 1.0f, -1.0f);
    
    CGContextDrawImage(context, CGRectMake(0.0, 0.0, width, height), image);
    
    CGContextRelease(context);
}

void FacetiousCppNSOpenGL::Imp::detectorThreadFunc()
{
    detector = new Aoc::CppCIDetector(Aoc::CppCIDetector::WorkerThread);
    
    Aut::RunningAverage<GLsizei> xAvg;
    Aut::RunningAverage<GLsizei> yAvg;
    Aut::RunningAverage<GLsizei> widthAvg;
    Aut::RunningAverage<GLsizei> heightAvg;
    
    bool keepGoing = true;
    while (keepGoing)
    {
        CGImageRef image = NULL;
        
        {
            std::unique_lock<std::mutex> lock (cameraImageMutex);
            std::chrono::seconds timeout(1);
            std::cv_status status(std::cv_status::no_timeout);
            
            while (!cameraImage && (status == std::cv_status::no_timeout))
                status = cameraImageCond.wait_for(lock, timeout);
            if (status == std::cv_status::no_timeout)
            {
                image = cameraImage;
                cameraImage = 0;
            }
        }
        
        if (image)
        {
            std::vector<Aoc::CppCIDetector::Face> faces;
            detector->detect(image, faces);
            
            float maxDim = 0;
            size_t iFaceMaxDim = 0;
            for (size_t i = 0; i < faces.size(); ++i)
            {
                float dim = (faces[i].width() > faces[i].height()) ?
                    faces[i].width() : faces[i].height();
                if (dim > maxDim)
                {
                    maxDim = dim;
                    iFaceMaxDim = i;
                }
            }
            
            if (!faces.empty())
            {
                GLsizei imageWidth = GLsizei(CGImageGetWidth(image));
                GLsizei imageHeight = GLsizei(CGImageGetHeight(image));
                GLubyte* newDetectorImage0 = detectorImagePool.alloc();
                textureDataFromImage(image, newDetectorImage0);

                detectedFace = faces[iFaceMaxDim];
                xAvg.add(detectedFace.x());
                yAvg.add(detectedFace.y());
                widthAvg.add(detectedFace.width());
                heightAvg.add(detectedFace.height());
                GLsizei x, y, width, height;
                if (stabilize)
                {
                    x = xAvg();
                    y = yAvg();
                    width = widthAvg();
                    height = heightAvg();
                }
                else
                {
                    x = detectedFace.x();
                    y = detectedFace.y();
                    width = detectedFace.width();
                    height = detectedFace.height();
                }

                while (width > detectorImageWidthMax)
                {
                    width -= width % 2;
                    GLubyte *newDetectorImage1 = detectorImagePool.alloc();
                    
                    const GLsizei bytesPerPixel = 4;
                    Agl::reduceImageBy2(newDetectorImage1, newDetectorImage0, width, height,
                                        bytesPerPixel, imageWidth, x, y);
                    
                    width /= 2;
                    height /= 2;
                    imageWidth = width;
                    imageHeight = height;
                    x = y = 0;
                    
                    detectorImagePool.free(newDetectorImage0);
                    newDetectorImage0 = newDetectorImage1;
                }
                {
                    std::lock_guard<std::mutex> lock( detectorMutex);
                    
                    delete detectorImage;
                    detectorImage = newDetectorImage0;
                    detectorImageWidth = imageWidth;
                    detectorImageHeight = imageHeight;
                    detectedFace = Aoc::CppCIDetector::Face(x, y, width, height);
                }
                
                requester->redraw();
            }
            
            CGImageRelease(image);
        }
        
        {
            std::lock_guard<std::mutex> lock(runDetectorThreadMutex);
            if (!runDetectorThread)
                keepGoing = false;
        }
    }
    
    delete detector;
#if 1
    // HEY!! Debugging output
    std::cerr << "detector thread: stopped running\n";
#endif
}

void FacetiousCppNSOpenGL::Imp::animTimerThreadFunc()
{
    const int fps = 30;
    const int sleepMs = 1.0f / fps * 1000;
    std::chrono::milliseconds sleepDuration(sleepMs);
    
    bool keepGoing = true;
    while (keepGoing)
    {
        std::this_thread::sleep_for(sleepDuration);
        
        {
            std::lock_guard<std::mutex> lock(animRunningMutex);
            if (anim.running())
                requester->redraw();
        }
        
        {
            std::lock_guard<std::mutex> lock(runAnimTimerThreadMutex);
            if (!runAnimTimerThread)
                keepGoing = false;
        }
    }
#if 1
    // HEY!! Debugging output
    std::cerr << "anim thread: stopped running\n";
#endif
}

//

FacetiousCppNSOpenGL::FacetiousCppNSOpenGL(Aoc::CppNSOpenGLRequester* r) :
    _m (new Imp(r))
{
    _m->camera = new Imp::Camera(_m.get());
    
    _m->camera->start();
    
    _m->runDetectorThread = true;
    _m->detectorThread = new std::thread(std::bind(&Imp::detectorThreadFunc, _m.get()));
    
    _m->runAnimTimerThread = true;
    _m->animTimerThread = new std::thread(std::bind(&Imp::animTimerThreadFunc, _m.get()));
}

FacetiousCppNSOpenGL::~FacetiousCppNSOpenGL()
{
    for (Agl::ShaderProgram* p : _m->frontShaderPrograms)
        delete p;
    
    for (Agl::ShaderProgram* p : _m->backShaderPrograms)
        delete p;
    
    for (Agl::Shader* s : _m->shaders)
        delete s;
    
    delete _m->frontSurface;
    delete _m->backSurface;
    
    delete _m->frontTexture;
    delete _m->backTexture;
    
    _m->camera->stop();
    delete _m->camera;
    
    {
        std::lock_guard<std::mutex> lock(_m->runDetectorThreadMutex);
        _m->runDetectorThread = false;
    }
    
    _m->detectorThread->join();
    delete _m->detectorThread;
    
    {
        std::lock_guard<std::mutex> lock(_m->runAnimTimerThreadMutex);
        _m->runAnimTimerThread = false;
    }
    
    _m->animTimerThread->join();
    delete _m->animTimerThread;
}

void FacetiousCppNSOpenGL::init()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glClearColor(0.4f, 0.4f, 0.5f, 1.0);

    const GLsizei resFront = 512;
    const GLsizei resBack = 256;
    const GLfloat bulgeBack = 0.1f;
    _m->frontSurface = new Agl::FlattishRectangularSurface(resFront, resFront);
    _m->backSurface = new Agl::FlattishRectangularSurface(resBack, resBack, bulgeBack);
    
    {
        IntensityHeightFieldVertexShader* vs = new IntensityHeightFieldVertexShader();
        _m->shaders.push_back(vs);
        _m->vertexShaders.push_back(vs);
        
        Agl::PhongOneDirectionalFragmentShader* fs = new Agl::PhongOneDirectionalFragmentShader();
        _m->shaders.push_back(fs);
        _m->phongFragmentShaders.push_back(fs);
        
        IntensityPhongShaderProgram* p = new IntensityPhongShaderProgram();
        _m->frontShaderPrograms.push_back(p);
        
        p->setVertexShader(vs);
        p->setFragmentShader(fs);
        p->addSurface(_m->frontSurface);
    }
    
    {
        IntensityHeightFieldVertexShader* vs = new IntensityHeightFieldVertexShader();
        _m->shaders.push_back(vs);
        _m->vertexShaders.push_back(vs);
        
        Agl::SphericalHarmonicsFragmentShader* fs = new Agl::SphericalHarmonicsFragmentShader();
        _m->shaders.push_back(fs);
        
        IntensityHarmonicsShaderProgram* p = new IntensityHarmonicsShaderProgram();
        _m->frontShaderPrograms.push_back(p);
        
        p->setVertexShader(vs);
        p->setFragmentShader(fs);
        p->addSurface(_m->frontSurface);
    }

    {
        Agl::BasicVertexShader* vs = new Agl::BasicVertexShader();
        _m->shaders.push_back(vs);
        _m->vertexShaders.push_back(vs);
        
        Agl::PhongOneDirectionalFragmentShader* fs = new Agl::PhongOneDirectionalFragmentShader();
        _m->shaders.push_back(fs);
        _m->phongFragmentShaders.push_back(fs);
        
        BasicPhongShaderProgram* p = new BasicPhongShaderProgram();
        _m->backShaderPrograms.push_back(p);
        
        p->setVertexShader(vs);
        p->setFragmentShader(fs);
        p->addSurface(_m->backSurface);
    }
    
    {
        Agl::BasicVertexShader* vs = new Agl::BasicVertexShader();
        _m->shaders.push_back(vs);
        _m->vertexShaders.push_back(vs);
        
        Agl::SphericalHarmonicsFragmentShader* fs = new Agl::SphericalHarmonicsFragmentShader();
        _m->shaders.push_back(fs);
        
        BasicHarmonicsShaderProgram* p = new BasicHarmonicsShaderProgram();
        _m->backShaderPrograms.push_back(p);
        
        p->setVertexShader(vs);
        p->setFragmentShader(fs);
        p->addSurface(_m->backSurface);
    }
    
    try
    {
        for (Agl::ShaderProgram* p : _m->frontShaderPrograms)
            p->build();
        for (Agl::ShaderProgram* p : _m->backShaderPrograms)
            p->build();
    }
    catch (const std::exception& exc)
    {
        Aut::fatalError(exc.what());
    }
    
    _m->frontSurface->buildElementBufferObject();
    _m->backSurface->buildElementBufferObject();
    
    _m->ambientColor = Imath::V3f(0.3f, 0.3f, 0.3f);
    _m->lightColor = Imath::V3f(0.6f, 0.6f, 0.6f);
    Imath::V3f lightDirection = Imath::V3f(1.0f, 1.0f, 1.0f).normalized();
    float shininess (20.0f);
    float strength (1.0f);
    
    for (Agl::PhongOneDirectionalFragmentShader* fs : _m->phongFragmentShaders)
    {
        fs->setAmbientColor(_m->ambientColor);
        fs->setLightColor(_m->lightColor);
        fs->setLightDirection(lightDirection);
        fs->setShininess(shininess);
        fs->setStrength(strength);
    }

#if 1
    // HEY!! How to handle the initial image?
    GLsizei frontTextureDimension = 64;
    CGImageRef image = imageFromFile("/Users/philip2/Documents/devel/osX/photo-30crop64.jpg");
    GLubyte* frontTextureColors = new GLubyte[CGImageGetWidth(image) * CGImageGetHeight(image) * 4];
    Imp::textureDataFromImage(image, frontTextureColors);
#endif

    _m->frontTexture = new Agl::TextureUbyte(GL_TEXTURE_2D);
    _m->frontTexture->build();
    _m->frontTexture->setData(frontTextureColors, frontTextureDimension, frontTextureDimension);
    _m->frontSurface->setTexture(_m->frontTexture);
    
    delete [] frontTextureColors;
    
    GLsizei backTextureDimension = 1;
    GLubyte backTextureColors[] = {255, 255, 255, 255};
    
    _m->backTexture = new Agl::TextureUbyte(GL_TEXTURE_2D);
    _m->backTexture->build();
    _m->backTexture->setData(backTextureColors, backTextureDimension, backTextureDimension);
    _m->backSurface->setTexture(_m->backTexture);
    
    std::vector<Aut::Anim<float>::Segment> animSegments;
    animSegments.push_back(Aut::Anim<float>::Segment(&_m->rotAngleY, 0, 50,
                                                     std::chrono::seconds(5)));
    animSegments.push_back(Aut::Anim<float>::Segment(&_m->rotAngleY, 50, -50,
                                                     std::chrono::seconds(10)));
    animSegments.push_back(Aut::Anim<float>::Segment(&_m->rotAngleY, -50, 0,
                                                     std::chrono::seconds(5)));
    animSegments.push_back(Aut::Anim<float>::Segment(&_m->rotAngleX, 0, 50,
                                                     std::chrono::seconds(5)));
    animSegments.push_back(Aut::Anim<float>::Segment(&_m->rotAngleX, 50, -50,
                                                     std::chrono::seconds(10)));
    animSegments.push_back(Aut::Anim<float>::Segment(&_m->rotAngleX, -50, 0,
                                                     std::chrono::seconds(5)));

    _m->anim.set(animSegments);
    _m->anim.start();
}

void FacetiousCppNSOpenGL::reshape(int width, int height)
{
    _m->viewWidth = width;
    _m->viewHeight = height;
    glViewport(0, 0, _m->viewWidth, _m->viewHeight);
}

void FacetiousCppNSOpenGL::draw()
{
#if 1
    // HEY!! Debugging output
    static size_t count = 0;
    std::cerr << "draw [" << count++ << "]\n";
#endif
    
    {
        std::unique_lock<std::mutex> lock(_m-> detectorMutex, std::try_to_lock);

        if (lock && _m->detectorImage)
        {
#if 1
            // HEY!! Debugging output
            std::cerr << "Face width " << _m->detectedFace.width()
                << " height " << _m->detectedFace.height() << "\n";
#endif
            
            GLsizei width = _m->detectedFace.width();
            GLint x = _m->detectedFace.x();
            GLint y = _m->detectedFace.y();
            
            GLsizei textureDim = width;
            GLint rowLength = _m->detectorImageWidth;
            GLint skipPixels = x;
            GLint skipRows = y;
            
            // Textures do not need to have power-of-two dimensions with modern
            // hardware: http://www.opengl.org/wiki/NPOT_Texture
            
            // TODO: Performance may be better if the code can use format GL_BGRA
            // and type GL_UNSIGNED_INT_8_8_8_8_REV.
            
            _m->frontTexture->setData(_m->detectorImage, textureDim, textureDim,
                                      GL_RGBA, GL_RGBA,
                                      rowLength, skipPixels, skipRows);
            
            _m->detectorImagePool.free(_m->detectorImage);
            _m->detectorImage = 0;
        }
    }
    
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
    float aspect = _m->viewWidth / float(_m->viewHeight);
    float near = 0.2f;
    float far = 5.0f;
    float h = 0.15f;
    float w = aspect * h;
    Imath::Frustumf frustum(near, far, -w, w, h, -h);
    Imath::M44f project = frustum.projectionMatrix();
    
    Imath::M44f view;
    view.setTranslation(Imath::V3f(0.0f, 0.0f, -1.0f));

    for (Agl::VertexShaderPNT* vs : _m->vertexShaders)
    {
        vs->setViewMatrix(view);
        vs->setProjectionMatrix(project);
    }
    
    {
        std::lock_guard<std::mutex> lock(_m->animRunningMutex);
        
        _m->anim.eval();
    }
    
    Imath::M44f frontRot, backRot;
    const float toRadians = M_PI / 180.0f;
    frontRot.setEulerAngles(Imath::V3f(_m->rotAngleX * toRadians, _m->rotAngleY * toRadians, 0));
    backRot.setEulerAngles(Imath::V3f(M_PI, 0.0f, 0.0f));
    backRot *= frontRot;
    _m->frontSurface->setModelMatrix(frontRot);
    _m->backSurface->setModelMatrix(backRot);
    
    for (Agl::PhongOneDirectionalFragmentShader* fs : _m->phongFragmentShaders)
    {
        fs->setAmbientColor(_m->ambientColor);
        fs->setLightColor(_m->lightColor);
    }
    
    try
    {
        _m->frontShaderPrograms[_m->iCurrentShaderProgram]->draw();
        _m->backShaderPrograms[_m->iCurrentShaderProgram]->draw();
    }
    catch (const std::exception& exc)
    {
        Aut::warning(exc.what());
    }
}

void FacetiousCppNSOpenGL::keyDown(Aoc::CppNSOpenGLBase::KeyEvent keyEvent)
{
    const float rotAngleChange = 10.0f;
    
    bool stopAnim = false;
    bool startAnim = false;
    
    if (keyEvent.special == KeyEvent::LeftArrow)
    {
        _m->rotAngleY -= rotAngleChange;
        stopAnim = true;
    }
    else if (keyEvent.special == KeyEvent::RightArrow)
    {
        _m->rotAngleY += rotAngleChange;
        stopAnim = true;
    }
    else if (keyEvent.special == KeyEvent::DownArrow)
    {
        _m->rotAngleX += rotAngleChange;
        stopAnim = true;
    }
    else if (keyEvent.special == KeyEvent::UpArrow)
    {
        _m->rotAngleX -= rotAngleChange;
        stopAnim = true;
    }
    else if (keyEvent.character == 'b')
    {
        if ((_m->ambientColor[0] < 1.0f) && (_m->lightColor[0] < 1.0f))
        {
            _m->ambientColor += Imath::V3f(0.1f);
            _m->lightColor += Imath::V3f(0.1f);
        }
    }
    else if (keyEvent.character == 'B')
    {
        if ((_m->ambientColor[0] > 0.0f) && (_m->lightColor[0] > 0.0f))
        {
            _m->ambientColor -= Imath::V3f(0.1f);
            _m->lightColor -= Imath::V3f(0.1f);
        }
    }
    else if (keyEvent.character == 'l')
    {
        _m->iCurrentShaderProgram =
            (_m->iCurrentShaderProgram + 1) % _m->frontShaderPrograms.size();
    }
    else if (keyEvent.character == 'r')
    {
        if (_m->detectorImageWidthMax > 32)
            _m->detectorImageWidthMax /= 2;
    }
    else if (keyEvent.character == 'R')
    {
        if (_m->detectorImageWidthMax < 2048)
            _m->detectorImageWidthMax *= 2;
    }
    else if (keyEvent.character == 's')
    {
        _m->stabilize = !_m->stabilize;
    }
    else if (keyEvent.character == ' ')
    {
        _m->rotAngleX = 0.0f;
        _m->rotAngleY = 0.0f;
        startAnim = true;
    }
    
    // According to the Mac Developer Library's "Thread Safety Summary",
    // this event-handling routine is run in the main thread that also
    // runs the draw() routine.  So there is no need for a lock to
    // prevent race conditions with the draw() routine for the access
    // to _m->rotAngleX and the other values set above.
    // But the _m->anim is accessed by _m->animTimerThread, so a lock
    // is needed for it.
    
    if (stopAnim || startAnim)
    {
        std::lock_guard<std::mutex> lock(_m->animRunningMutex);
        
        if (startAnim)
            _m->anim.start();
        else if (stopAnim)
            _m->anim.stop();
    }
    
    _m->requester->redraw();
}
