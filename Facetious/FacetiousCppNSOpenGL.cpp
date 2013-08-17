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

#include "Aoc/AocCppAVFoundationCamera.h"
#include "Aoc/AocCppCIDetector.h"

#include "Agl/AglUtilities.h"
#include "Agl/AglShader.h"
#include "Agl/AglImagePool.h"
#include "Agl/AglTextureUbyte.h"
#include "Agl/AglFlattishRectangularSurface.h"
#include "Agl/AglBasicVertexShader.h"
#include "Agl/AglPhongOneDirectionalFragmentShader.h"
#include "Agl/AglSphericalHarmonicsFragmentShader.h"

#include "Aut/AutAlert.h"
#include "Aut/AutAnim.h"
#include "Aut/AutRunningAverage.h"

#include <OpenEXR/ImathFrustum.h>
#include <OpenEXR/ImathMatrix.h>
#include <OpenEXR/ImathVec.h>

#include <OpenGL/gl3.h>
#include <ImageIO/CGImageSource.h>

#include <thread>
#include <chrono>
#include <deque>
#include <assert.h>

class FacetiousCppNSOpenGL::Imp
{
public:
    
    Imp(Aoc::CppNSOpenGLRequester* r) :
        camera(0), cameraImage(0), detectorThread(0), detector(0),
        runDetectorThread(true), detectorImage(0), detectorImageWidthMax(64),
        detectorImageWidth(0), detectorImageHeight(0), stabilize(true),
        animTimerThread(0), requester(r), runAnimTimerThread(true),
        iCurrentShaderProgram(0), frontSurface(0), backSurface(0),
        frontTexture(0), backTexture(0), viewWidth(0), viewHeight(0),
        rotAngleX(0.0f), rotAngleY(0.0f) {}
    
    // The caller allocates and owns "data".
    
    static void         getTextureDataFromImage(CGImageRef image,
                                                GLubyte* data);
    
    // Allocates "data" but the caller owns it.
    
    static void         getDefaultImage(GLubyte*& data, GLsizei& width,
                                        GLsizei& height);
    
    void                detectorThreadFunc();
    void                animTimerThreadFunc();
    
    // A derived camera class that handles captured images by making them
    // available to the face detector.
    
    class Camera : public Aoc::CppAVFoundationCamera
    {
    public:
        Camera(FacetiousCppNSOpenGL::Imp* imp);
        virtual void    handleCapturedImage(CGImageRef image);
        
    private:
        FacetiousCppNSOpenGL::Imp*     _appImp;
    };
    
    Camera*                            camera;
    
    // Camera::handleCapturedImage() runs in a system thread, and a
    // condition variable tells the application when an image is ready.
    
    std::mutex                         cameraImageMutex;
    std::condition_variable            cameraImageCond;
    CGImageRef                         cameraImage;
    
    // The face detector is the slowest component of the system, so it runs in
    // its own thread, allowing rendering to proceed asynchronously with the
    // latest detected face.
    
    std::thread*                       detectorThread;
    Aoc::CppCIDetector*                detector;
    
    // For stopping the face detector thread.
    
    std::mutex                         runDetectorThreadMutex;
    bool                               runDetectorThread;
    
    // A mutex protects data shared by the face detector thread and the
    // rendering code in the main thread.
    // The image pool simplifies management of the source and destination
    // images when reducing the image resolution.
    
    std::mutex                         detectorMutex;
    Agl::ImagePool                     detectorImagePool;
    GLubyte*                           detectorImage;
    GLsizei                            detectorImageWidthMax;
    GLsizei                            detectorImageWidth;
    GLsizei                            detectorImageHeight;
    Aoc::CppCIDetector::Face           detectedFace;
    
    bool                               stabilize;
    
    // Another thread handles the timing of the animation by generating regular
    // redraw requests via the requester.
    
    std::thread*                       animTimerThread;
    
    Aoc::CppNSOpenGLRequester*         requester;
    
    // For stopping the animation timer thread.
    
    std::mutex                         runAnimTimerThreadMutex;
    bool                               runAnimTimerThread;
    
    // The Aut::Anim instance that does the animation is shared between
    // the animation timer thread and the main thread so it is protected
    // with a mutex.
    
    std::mutex                         animMutex;
    Aut::Anim<float>                   anim;
    
    // Shaders and shader programs.
    
    std::vector<Agl::VertexShaderPNT*> vertexShaders;
    std::vector<Agl::Shader*>          fragmentShaders;
    std::vector<Agl::PhongOneDirectionalFragmentShader*>
                                       phongFragmentShaders;
    std::vector<Agl::ShaderProgram*>   frontShaderPrograms;
    std::vector<Agl::ShaderProgram*>   backShaderPrograms;
    size_t                             iCurrentShaderProgram;
    
    // Surfaces and textures.
    
    Agl::FlattishRectangularSurface*   frontSurface;
    Agl::FlattishRectangularSurface*   backSurface;
    
    Agl::TextureUbyte*                 frontTexture;
    Agl::TextureUbyte*                 backTexture;
    
    // Other rendering-related data.
    
    int                                viewWidth;
    int                                viewHeight;
    
    float                              rotAngleX;
    float                              rotAngleY;
    
    Imath::V3f                         ambientColor;
    Imath::V3f                         lightColor;
};

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
    
    if (_appImp->detectorImagePool.imageWidth() == 0)
    {
        // Initialize the image pool.
        
        const GLsizei bytesPerPixel = 4;
        _appImp->detectorImagePool.setImageSize(GLsizei(CGImageGetWidth(image)),
                                                GLsizei(CGImageGetHeight(image)),
                                                bytesPerPixel);
    }
    
    _appImp->cameraImageCond.notify_one();
}

//

void FacetiousCppNSOpenGL::Imp::getTextureDataFromImage (CGImageRef image,
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

void FacetiousCppNSOpenGL::Imp::getDefaultImage(GLubyte*& data, GLsizei& width,
                                                GLsizei& height)
{
    CFBundleRef bundle = CFBundleGetMainBundle();
    CFStringRef name = CFStringCreateWithCString (NULL, "defaultImage",
                                                  kCFStringEncodingUTF8);
    
    CFURLRef url = CFBundleCopyResourceURL(bundle, name, CFSTR("jpg"), NULL);
    CFRelease(name);
    
    CGDataProviderRef provider = CGDataProviderCreateWithURL(url);
    CFRelease (url);
    
    CGImageRef image =
        CGImageCreateWithJPEGDataProvider (provider, NULL, true,
                                           kCGRenderingIntentDefault);
    CGDataProviderRelease (provider);

    width = GLsizei(CGImageGetWidth(image));
    height = GLsizei(CGImageGetHeight(image));
    GLsizei bytesPerPixel = 4;
    data = new GLubyte [width * height * bytesPerPixel];
    
    getTextureDataFromImage(image, data);
}

void FacetiousCppNSOpenGL::Imp::detectorThreadFunc()
{
    // Create the face detector, telling it that it is in its own thread
    // so it needs its own Objective-C autorelease pool.
    
    detector = new Aoc::CppCIDetector(Aoc::CppCIDetector::WorkerThread);
    
    // Running averages for stabilizing the detected face.
    
    Aut::RunningAverage<GLsizei> xAvg;
    Aut::RunningAverage<GLsizei> yAvg;
    Aut::RunningAverage<GLsizei> widthAvg;
    Aut::RunningAverage<GLsizei> heightAvg;
    
    bool keepGoing = true;
    while (keepGoing)
    {
        CGImageRef image = NULL;
        
        {
            // Wait for an image from the camera.  But stop waiting every
            // second so the end of the loop can check whether the application
            // is shutting down and wanting the thread to stop.
            
            std::unique_lock<std::mutex> lock (cameraImageMutex);
            std::chrono::seconds timeout(1);
            std::cv_status status(std::cv_status::no_timeout);
            
            // Set "image" to the latest camera image when it is available.
            
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
            // Detect faces in the latest camera image.
            
            std::vector<Aoc::CppCIDetector::Face> faces;
            detector->detect(image, faces);
            
            // Choose the face with the maximum dimension.
            
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
                // If a face was found, convert the camera image into
                // texture data, using the image pool to avoid repeated
                // reallocations.
                
                GLsizei imageWidth = GLsizei(CGImageGetWidth(image));
                GLsizei imageHeight = GLsizei(CGImageGetHeight(image));
                GLubyte* newDetectorImage0 = detectorImagePool.alloc();
                getTextureDataFromImage(image, newDetectorImage0);

                detectedFace = faces[iFaceMaxDim];
                xAvg.add(detectedFace.x());
                yAvg.add(detectedFace.y());
                widthAvg.add(detectedFace.width());
                heightAvg.add(detectedFace.height());
                
                // Apply stabilization to the detected face region if
                // requested.
                
                GLsizei x = stabilize ? xAvg() : detectedFace.x();
                GLsizei y = stabilize ? yAvg() : detectedFace.y();
                GLsizei width = stabilize ? widthAvg() : detectedFace.width();
                GLsizei height = stabilize ? heightAvg() : detectedFace.height();

                // Reduce the image to below the maximum requested width.
                // This width is user settable, but in general, the results of
                // IntensityHeightFieldVertexShader look best when the image
                // relatively low resolution, like 64 x 64.

                while (width > detectorImageWidthMax)
                {
                    // Repeatedly reduce the image by a factor of 2 in each
                    // dimension.  This simple approach has good enough
                    // performance in practice.
                    
                    width -= width % 2;
                    GLubyte *newDetectorImage1 = detectorImagePool.alloc();
                    
                    const GLsizei bytesPerPixel = 4;
                    Agl::reduceImageBy2(newDetectorImage1, newDetectorImage0,
                                        width, height, bytesPerPixel,
                                        imageWidth, x, y);
                    
                    width /= 2;
                    height /= 2;
                    imageWidth = width;
                    imageHeight = height;
                    x = y = 0;
                    
                    detectorImagePool.free(newDetectorImage0);
                    newDetectorImage0 = newDetectorImage1;
                }
                
                // Make the detected face available to the main thread for
                // rendering.
                
                {
                    std::lock_guard<std::mutex> lock( detectorMutex);
                    
                    delete detectorImage;
                    detectorImage = newDetectorImage0;
                    detectorImageWidth = imageWidth;
                    detectorImageHeight = imageHeight;
                    detectedFace = Aoc::CppCIDetector::Face(x, y, width, height);
                }
                
                // Request the rendering.
                
                requester->redraw();
            }
            
            CGImageRelease(image);
        }
        
        // End this routine if the application is shutting down and needs
        // the thread to stop.
        
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
    // This thread simply generates regular requests to rerender with the
    // latest animation settings.  (The main thread calls Aut::Anim to
    // get the latest animation.)
    
    const int fps = 30;
    const int sleepMs = 1.0f / fps * 1000;
    std::chrono::milliseconds sleepDuration(sleepMs);
    
    bool keepGoing = true;
    while (keepGoing)
    {
        std::this_thread::sleep_for(sleepDuration);
        
        {
            std::lock_guard<std::mutex> lock(animMutex);
            if (anim.running())
                requester->redraw();
        }
        
        // End this routine if the application is shutting down and needs
        // the thread to stop.
        
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
    _m->detectorThread =
        new std::thread(std::bind(&Imp::detectorThreadFunc, _m.get()));
    
    _m->runAnimTimerThread = true;
    _m->animTimerThread =
        new std::thread(std::bind(&Imp::animTimerThreadFunc, _m.get()));
}

FacetiousCppNSOpenGL::~FacetiousCppNSOpenGL()
{
    for (Agl::ShaderProgram* p : _m->frontShaderPrograms)
        delete p;
    for (Agl::ShaderProgram* p : _m->backShaderPrograms)
        delete p;
    
    for (Agl::Shader* s : _m->vertexShaders)
        delete s;
    for (Agl::Shader* s : _m->fragmentShaders)
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
    // General OpenGL initialization.
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glClearColor(0.4f, 0.4f, 0.5f, 1.0);

    // Initialize the front and back surfaces.  The front surface is flat
    // at this point, but will be heights computed at each vertex based on
    // the image of the detected face by IntensityHeightFieldVertexShader.
    // The back surface has a bit of a bulge, to make it more interesting.
    
    const GLsizei resFront = 512;
    const GLsizei resBack = 256;
    const GLfloat bulgeBack = 0.1f;
    _m->frontSurface = new Agl::FlattishRectangularSurface(resFront, resFront);
    _m->backSurface = new Agl::FlattishRectangularSurface(resBack, resBack,
                                                          bulgeBack);
    
    // Initialize the shaders and the shader programs.  There are two programs
    // for each surface, for the two different fragments shaders implementing
    // two different lighting models.  The front and the back surfaces use
    // different vertex shaders, because only the front surface should have
    // heights computed at each vertex by IntensityHeightFieldVertexShader.
    
    IntensityHeightFieldVertexShader* vs0 = new IntensityHeightFieldVertexShader();
    _m->vertexShaders.push_back(vs0);
    
    Agl::PhongOneDirectionalFragmentShader* fs0 = new Agl::PhongOneDirectionalFragmentShader();
    _m->fragmentShaders.push_back(fs0);
    _m->phongFragmentShaders.push_back(fs0);
    
    IntensityPhongShaderProgram* p0 = new IntensityPhongShaderProgram();
    _m->frontShaderPrograms.push_back(p0);
    
    p0->setVertexShader(vs0);
    p0->setFragmentShader(fs0);
    p0->addSurface(_m->frontSurface);

    IntensityHeightFieldVertexShader* vs1 = new IntensityHeightFieldVertexShader();
    _m->vertexShaders.push_back(vs1);
    
    Agl::SphericalHarmonicsFragmentShader* fs1 = new Agl::SphericalHarmonicsFragmentShader();
    _m->fragmentShaders.push_back(fs1);
    
    IntensityHarmonicsShaderProgram* p1 = new IntensityHarmonicsShaderProgram();
    _m->frontShaderPrograms.push_back(p1);
    
    p1->setVertexShader(vs1);
    p1->setFragmentShader(fs1);
    p1->addSurface(_m->frontSurface);

    Agl::BasicVertexShader* vs2 = new Agl::BasicVertexShader();
    _m->vertexShaders.push_back(vs2);
    
    Agl::PhongOneDirectionalFragmentShader* fs2 = new Agl::PhongOneDirectionalFragmentShader();
    _m->fragmentShaders.push_back(fs2);
    _m->phongFragmentShaders.push_back(fs2);
    
    BasicPhongShaderProgram* p2 = new BasicPhongShaderProgram();
    _m->backShaderPrograms.push_back(p2);
    
    p2->setVertexShader(vs2);
    p2->setFragmentShader(fs2);
    p2->addSurface(_m->backSurface);

    Agl::BasicVertexShader* vs3 = new Agl::BasicVertexShader();
    _m->vertexShaders.push_back(vs3);
    
    Agl::SphericalHarmonicsFragmentShader* fs3 = new Agl::SphericalHarmonicsFragmentShader();
    _m->fragmentShaders.push_back(fs3);
    
    BasicHarmonicsShaderProgram* p3 = new BasicHarmonicsShaderProgram();
    _m->backShaderPrograms.push_back(p3);
    
    p3->setVertexShader(vs3);
    p3->setFragmentShader(fs3);
    p3->addSurface(_m->backSurface);
    
    // Build the shaders, shader programs and element buffers.
    
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
    
    // Initialize the Phong shaders' light.
    
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

    GLubyte* frontTextureColors;
    GLsizei frontTextureWidth, frontTextureHeight;
    Imp::getDefaultImage(frontTextureColors, frontTextureWidth, frontTextureHeight);

    _m->frontTexture = new Agl::TextureUbyte(GL_TEXTURE_2D);
    _m->frontTexture->build();
    _m->frontTexture->setData(frontTextureColors, frontTextureWidth, frontTextureHeight);
    _m->frontSurface->setTexture(_m->frontTexture);
    
    delete [] frontTextureColors;
    
    // The back surface is meant to be a solid white, so it has a very
    // simple texture.
    
    GLsizei backTextureDimension = 1;
    GLubyte backTextureColors[] = {255, 255, 255, 255};
    
    _m->backTexture = new Agl::TextureUbyte(GL_TEXTURE_2D);
    _m->backTexture->build();
    _m->backTexture->setData(backTextureColors, backTextureDimension, backTextureDimension);
    _m->backSurface->setTexture(_m->backTexture);
    
    // Set up the animation.  It involves rotating to the left, to the right,
    // and back to the center, then rotating down, and up, and back to the
    // center.  This pattern then repeats.
    
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
            
            // If a new image is available from the detector thread, use
            // it to replace front surface's texture.

            GLsizei width = _m->detectedFace.width();
            GLint x = _m->detectedFace.x();
            GLint y = _m->detectedFace.y();
            
            GLsizei textureDim = width;
            GLint rowLength = _m->detectorImageWidth;
            GLint skipPixels = x;
            GLint skipRows = y;
            
            // Textures do not need to have power-of-two dimensions with modern
            // hardware: http://www.opengl.org/wiki/NPOT_Texture
            
            // TODO: Performance may be better if the code can use format
            // GL_BGRA and type GL_UNSIGNED_INT_8_8_8_8_REV.
            
            _m->frontTexture->setData(_m->detectorImage, textureDim, textureDim,
                                      GL_RGBA, GL_RGBA,
                                      rowLength, skipPixels, skipRows);
            
            _m->detectorImagePool.free(_m->detectorImage);
            _m->detectorImage = 0;
        }
    }
    
    // Prepare to render the new frame.
    
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Set up the projection and view matrices.
    
    float aspect = _m->viewWidth / float(_m->viewHeight);
    float h = 0.15f, w = aspect * h;
    float near = 0.2f, far = 5.0f;
    Imath::Frustumf frustum(near, far, -w, w, h, -h);
    Imath::M44f project = frustum.projectionMatrix();
    
    Imath::M44f view;
    view.setTranslation(Imath::V3f(0.0f, 0.0f, -1.0f));

    for (Agl::VertexShaderPNT* vs : _m->vertexShaders)
    {
        vs->setViewMatrix(view);
        vs->setProjectionMatrix(project);
    }
    
    // Get the latest animation for the rotaton angles, and apply it to
    // the surfaces' model matrices.
    
    {
        std::lock_guard<std::mutex> lock(_m->animMutex);
        
        _m->anim.eval();
    }
    
    Imath::M44f frontRot, backRot;
    const float toRadians = M_PI / 180.0f;
    frontRot.setEulerAngles(Imath::V3f(_m->rotAngleX * toRadians, _m->rotAngleY * toRadians, 0));
    backRot.setEulerAngles(Imath::V3f(M_PI, 0.0f, 0.0f));
    backRot *= frontRot;
    _m->frontSurface->setModelMatrix(frontRot);
    _m->backSurface->setModelMatrix(backRot);
    
    // Update the shaders with parameters the user might have changed.
    
    for (Agl::PhongOneDirectionalFragmentShader* fs : _m->phongFragmentShaders)
    {
        fs->setAmbientColor(_m->ambientColor);
        fs->setLightColor(_m->lightColor);
    }
    
    // Render the surfaces with the user's current choice for the shader
    // programs.
    
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
    // Handle any keyboard input from the user.
    
    const float rotAngleChange = 10.0f;
    
    bool stopAnim = false;
    bool startAnim = false;
    
    if (keyEvent.special == KeyEvent::LeftArrow)
    {
        // Arrow keys stop the animation and change the rotation angles
        // explicitly.
        
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
        // 'b'/'B' for "brighten".
        
        if ((_m->ambientColor[0] < 1.0f) && (_m->lightColor[0] < 1.0f))
        {
            _m->ambientColor += Imath::V3f(0.1f);
            _m->lightColor += Imath::V3f(0.1f);
        }
    }
    else if (keyEvent.character == 'B')
    {
        // 'b'/'B' for "brighten".
        
        if ((_m->ambientColor[0] > 0.0f) && (_m->lightColor[0] > 0.0f))
        {
            _m->ambientColor -= Imath::V3f(0.1f);
            _m->lightColor -= Imath::V3f(0.1f);
        }
    }
    else if (keyEvent.character == 'l')
    {
        // 'l' for "lighting".
        
        _m->iCurrentShaderProgram =
            (_m->iCurrentShaderProgram + 1) % _m->frontShaderPrograms.size();
    }
    else if (keyEvent.character == 'r')
    {
        // 'r'/'R' for "resolution".
        
        if (_m->detectorImageWidthMax > 32)
            _m->detectorImageWidthMax /= 2;
    }
    else if (keyEvent.character == 'R')
    {
        // 'r'/'R' for "resolution".
        
        if (_m->detectorImageWidthMax < 2048)
            _m->detectorImageWidthMax *= 2;
    }
    else if (keyEvent.character == 's')
    {
        // 's' for "stabilize".
        
        _m->stabilize = !_m->stabilize;
    }
    else if (keyEvent.character == ' ')
    {
        // Spacebar restarts the animation at the beginning.
        
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
        std::lock_guard<std::mutex> lock(_m->animMutex);
        
        if (startAnim)
            _m->anim.start();
        else if (stopAnim)
            _m->anim.stop();
    }
    
    _m->requester->redraw();
}
