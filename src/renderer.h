#ifndef _RENDERER_H
#define _RENDERER_H
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "camera.h"
#include "glad/glad.h"
#include "rectangle.h"
#include "scene.h"
#include "shader.h"

class Renderer {
 private:
  unsigned int samples;
  glm::uvec2 resolution;
  Camera camera;
  Scene scene;

  GLuint accumTexture;
  GLuint stateTexture;
  GLuint accumFBO;

  GLuint materialUBO;
  GLuint primitiveUBO;

  Rectangle rectangle;

  Shader pt_shader;
  Shader output_shader;
  Shader normal_shader;

  enum class RenderMode {
    Render,
    Normal,
  };

  RenderMode mode;

  bool clear_flag;

 public:
  Renderer(unsigned int width, unsigned int height)
      : samples(0),
        resolution({width, height}),
        scene(),
        rectangle(),
        pt_shader({"./shaders/pt.vert", "./shaders/pt.frag"}),
        output_shader({"./shaders/pt.vert", "./shaders/output.frag"}),
        normal_shader({"./shaders/pt.vert", "./shaders/normal.frag"}),
        mode(RenderMode::Render) {
    // setup accumulate texture
    glGenTextures(1, &accumTexture);
    glBindTexture(GL_TEXTURE_2D, accumTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB,
                 GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // setup RNG state texture
    glGenTextures(1, &stateTexture);
    glBindTexture(GL_TEXTURE_2D, stateTexture);
    std::vector<uint32_t> seed(width * height);
    std::random_device rnd_dev;
    std::mt19937 mt(rnd_dev());
    std::uniform_int_distribution<uint32_t> dist(
        1, std::numeric_limits<uint32_t>::max());
    for (unsigned int i = 0; i < seed.size(); ++i) {
      seed[i] = dist(mt);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_INT, seed.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // setup accumulate FBO
    glGenFramebuffers(1, &accumFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           accumTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           stateTexture, 0);
    GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, attachments);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // setup UBO
    glGenBuffers(1, &materialUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, materialUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Material) * 100, NULL,
                 GL_STATIC_DRAW);
    glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    sizeof(Material) * scene.materials.size(),
                    scene.materials.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, materialUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glGenBuffers(1, &primitiveUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, primitiveUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Primitive) * 100, NULL,
                 GL_STATIC_DRAW);
    glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    sizeof(Primitive) * scene.primitives.size(),
                    scene.primitives.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, primitiveUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // set uniforms
    pt_shader.setUniform("resolution", resolution);
    pt_shader.setUniform("resolutionYInv", 1.0f / resolution.y);
    pt_shader.setUniformTexture("accumTexture", accumTexture, 0);
    pt_shader.setUniformTexture("stateTexture", stateTexture, 1);
    pt_shader.setUBO("MaterialBlock", 0);
    pt_shader.setUBO("PrimitiveBlock", 1);

    output_shader.setUniformTexture("accumTexture", accumTexture, 0);

    normal_shader.setUniform("resolution", resolution);
    normal_shader.setUniform("resolutionYInv", 1.0f / resolution.y);
    normal_shader.setUBO("PrimitiveBlock", 1);
  }

  unsigned int getWidth() const { return resolution.x; }
  unsigned int getHeight() const { return resolution.y; }
  unsigned int getSamples() const { return samples; }

  glm::vec3 getCameraPosition() const { return camera.camPos; }

  void moveCamera(const glm::vec3& v) {
    camera.move(v);
    clear_flag = true;
  }
  void orbitCamera(float dTheta, float dPhi) {
    camera.orbit(dTheta, dPhi);
    clear_flag = true;
  }

  void render() {
    if (clear_flag) {
      clear();
      clear_flag = false;
    }

    // path tracing
    glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
    pt_shader.setUniform("camPos", camera.camPos);
    pt_shader.setUniform("camForward", camera.camForward);
    pt_shader.setUniform("camRight", camera.camRight);
    pt_shader.setUniform("camUp", camera.camUp);

    glViewport(0, 0, resolution.x, resolution.y);
    rectangle.draw(pt_shader);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // update samples
    samples++;

    // output
    output_shader.setUniform("samplesInv", 1.0f / samples);
    rectangle.draw(output_shader);
  }

  void clear() {
    // clear accumTexture
    glBindTexture(GL_TEXTURE_2D, accumTexture);
    std::vector<GLfloat> data(3 * resolution.x * resolution.y);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, resolution.x, resolution.y, GL_RGB,
                    GL_FLOAT, data.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // update texture uniforms
    pt_shader.setUniformTexture("accumTexture", accumTexture, 0);
    output_shader.setUniformTexture("accumTexture", accumTexture, 0);

    // reset samples
    samples = 0;
  }

  void resize(unsigned int width, unsigned int height) {
    // update resolution
    resolution = glm::uvec2(width, height);
    pt_shader.setUniform("resolution", resolution);
    pt_shader.setUniform("resolutionYInv", 1.0f / resolution.y);
    output_shader.setUniform("resolution", resolution);

    // resize textures
    glBindTexture(GL_TEXTURE_2D, accumTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB,
                 GL_FLOAT, 0);

    glBindTexture(GL_TEXTURE_2D, stateTexture);
    std::vector<uint32_t> seed(width * height);
    std::random_device rnd_dev;
    std::mt19937 mt(rnd_dev());
    std::uniform_int_distribution<uint32_t> dist(
        1, std::numeric_limits<uint32_t>::max());
    for (unsigned int i = 0; i < seed.size(); ++i) {
      seed[i] = dist(mt);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_INT, seed.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // clear textures
    clear();
  }
};

#endif