// Realtime Lift and Drag SURP 2018
// Christian Eckhart, William Newey, Austin Quick, Sebastian Seibert


// Allows program to be run on dedicated graphics processor for laptops with
// both integrated and dedicated graphics using Nvidia Optimus
#ifdef _WIN32
extern "C" {
    _declspec(dllexport) unsigned int NvOptimusEnablement(1);
}
#endif

// TODO: do we care about z world space?
// TODO: can we assume square?
// TODO: idea of and air frame or air space
// TODO: lift proportional to slice size?



#include <iostream>

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "Simulation.hpp"
#include "Model.hpp"
#include "Program.h"
#include "WindowManager.h"



enum class SimModel { airfoil, f18, sphere };



static constexpr SimModel k_simModel(SimModel::f18);

static const std::string k_defResourceDir("resources");

static constexpr float k_minAngleOfAttack(-90.0f), k_maxAngleOfAttack(90.0f);
static constexpr float k_minRudderAngle(-90.0f), k_maxRudderAngle(90.0f);
static constexpr float k_minAileronAngle(-90.0f), k_maxAileronAngle(90.0f);
static constexpr float k_minElevatorAngle(-90.0f), k_maxElevatorAngle(90.0f);

static constexpr float k_angleOfAttackIncrement(1.0f); // the granularity of angle of attack
static constexpr float k_manualAngleIncrement(1.0f); // how many degrees to change the rudder, elevator, and ailerons by when using arrow keys
static constexpr float k_autoAngleIncrement(7.0f); // how many degrees to change the angle of attack by when auto progressing



static std::string s_resourceDir(k_defResourceDir);
static std::shared_ptr<Program> s_phongProg;


static std::unique_ptr<Model> s_model;
static mat4 s_modelMat;
static mat3 s_normalMat;
static vec3 s_centerOfGravity; // Center of gravity of the model in world space
static vec3 s_vel;
static vec3 s_pos;
static vec3 s_lift;
static vec3 s_drag;

//all in degrees
static float s_angleOfAttack(0.0f);
static float s_aileronAngle(0.0f);
static float s_rudderAngle(0.0f);
static float s_elevatorAngle(0.0f);

WindowManager *windowManager = nullptr;
static GLFWwindow * s_mainWindow;
static bool s_shouldStep(false);
static bool s_shouldSweep(true);
static bool s_shouldAutoProgress(false);

static std::shared_ptr<Program> s_texProg;
static uint s_screenVAO;
static uint s_screenVBO;

static constexpr int k_width(720), k_height(480);

static void printUsage() {
    std::cout << "Usage: liftdrag [resource_directory]" << std::endl;
}

static bool processArgs(int argc, char ** argv) {
    if (argc > 2) {
        printUsage();
        return false;
    }

    if (argc == 2) {
        s_resourceDir = argv[1];
    }

    return true;
}

static void changeAngleOfAttack(float deltaAngle) {
    s_angleOfAttack = glm::clamp(s_angleOfAttack + deltaAngle, -k_maxAngleOfAttack, k_maxAngleOfAttack);

    std::cout << "Angle of attack set to " << s_angleOfAttack << std::endl;
}

static void changeRudderAngle(float deltaAngle) {
    s_rudderAngle = glm::clamp(s_rudderAngle + deltaAngle, -k_maxRudderAngle, k_maxRudderAngle);

    mat4 modelMat(glm::rotate(mat4(), s_rudderAngle, vec3(0.0f, 1.0f, 0.0f)));
    mat3 normalMat(modelMat);
    s_model->subModel("RudderL01")->localTransform(modelMat, normalMat);
    s_model->subModel("RudderR01")->localTransform(modelMat, normalMat);

    std::cout << "Rudder angle set to " << s_rudderAngle << std::endl;
}

static void changeAileronAngle(float deltaAngle) {
    s_aileronAngle = glm::clamp(s_aileronAngle + deltaAngle, -k_maxAileronAngle, k_maxAileronAngle);

    mat4 modelMat(glm::rotate(mat4(), s_aileronAngle, vec3(1.0f, 0.0f, 0.0f)));
    mat3 normalMat(modelMat);
    s_model->subModel("AileronL01")->localTransform(modelMat, normalMat);

    modelMat = glm::rotate(mat4(), -s_aileronAngle, vec3(1.0f, 0.0f, 0.0f));
    normalMat = modelMat;
    s_model->subModel("AileronR01")->localTransform(modelMat, normalMat);

    std::cout << "Aileron angle set to " << s_aileronAngle << std::endl;
}

static void changeElevatorAngle(float deltaAngle) {
    s_elevatorAngle = glm::clamp(s_elevatorAngle + deltaAngle, -k_maxElevatorAngle, k_maxElevatorAngle);

    mat4 modelMat(glm::rotate(mat4(), s_elevatorAngle, vec3(1.0f, 0.0f, 0.0f)));
    mat3 normalMat(modelMat);
    s_model->subModel("ElevatorL01")->localTransform(modelMat, normalMat);
    s_model->subModel("ElevatorR01")->localTransform(modelMat, normalMat);

    std::cout << "Elevator angle set to " << s_elevatorAngle << std::endl;
}

static void set(const mat4 & modelMat, const mat3 & normalMat, const vec3 & centerOfGravity) {
    s_modelMat = modelMat;
    s_normalMat = normalMat;
    s_centerOfGravity = centerOfGravity;
}

static void setSimulation(float angleOfAttack, bool debug) {
    mat4 modelMat;
    mat3 normalMat;
    float depth;
    vec3 centerOfGravity;

    switch (k_simModel) {
        case SimModel::airfoil:
            modelMat = glm::translate(mat4(), vec3(0.0f, 0.0f, 0.5f)) * modelMat;
            modelMat = glm::rotate(mat4(), glm::radians(-angleOfAttack), vec3(1.0f, 0.0f, 0.0f)) * modelMat;
            modelMat = glm::translate(mat4(), vec3(0.0f, 0.0f, -0.5f)) * modelMat;
            modelMat = glm::scale(mat4(), vec3(0.875f, 1.0f, 1.0f)) * modelMat;
            normalMat = glm::transpose(glm::inverse(modelMat));
            depth = 1.0f;
            centerOfGravity = vec3(0.0f, 0.0f, depth * 0.5f);
            break;

        case SimModel::f18:
            modelMat = glm::scale(mat4(), vec3(0.10f));
            modelMat = glm::rotate(mat4(), glm::pi<float>(), vec3(0.0f, 0.0f, 1.0f)) * modelMat;
            modelMat = glm::rotate(mat4(1.0f), glm::radians(-angleOfAttack), vec3(1.0f, 0.0f, 0.0f)) * modelMat;
            modelMat = glm::translate(mat4(), vec3(0.0f, 0.0f, -1.1f)) * modelMat;
            normalMat = glm::transpose(glm::inverse(modelMat));
            depth = 2.0f;
            centerOfGravity = vec3(0.0f, 0.0f, depth * 0.5f);
            break;

        case SimModel::sphere:
            modelMat = glm::rotate(mat4(), glm::radians(-angleOfAttack), vec3(1.0f, 0.0f, 0.0f));
            modelMat = glm::scale(mat4(), vec3(0.5f)) * modelMat;
            modelMat = glm::translate(mat4(), vec3(0.0f, 0.0f, -0.5f)) * modelMat;
            normalMat = glm::transpose(glm::inverse(modelMat));
            depth = 1.0f;
            centerOfGravity = vec3(0.0f, 0.0f, depth * 0.5f);
            break;
    }

    Simulation::set(*s_model, modelMat, normalMat, depth, centerOfGravity, debug);
    set(modelMat, normalMat, centerOfGravity);
}

static void doFastSweep(float angleOfAttack) {
    setSimulation(angleOfAttack, false);

    double then(glfwGetTime());
    Simulation::sweep();
    double dt(glfwGetTime() - then);
    s_lift = Simulation::lift();
    s_drag = Simulation::drag();
    printf("Angle: %f, Lift: (%f, %f, %f), Drag (%f, %f, %f), SPS: %f\n",
        angleOfAttack,
        s_lift.x, s_lift.y, s_lift.z,
        s_drag.x, s_drag.y, s_drag.z,
        (1.0 / dt)
    );
}

static void doAllAngles() {
    for (float angle(k_minAngleOfAttack); angle <= k_maxAngleOfAttack; angle += k_angleOfAttackIncrement) {
        doFastSweep(angle);
        glfwMakeContextCurrent(s_mainWindow);
    }
}

static void errorCallback(int error, const char * description) {
    std::cerr << "GLFW error: " << description << std::endl;
}

static void keyCallback(GLFWwindow * window, int key, int scancode, int action, int mods) {
    // If space bar is pressed, do one slice
    if (key == GLFW_KEY_SPACE && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        s_shouldStep = true;
        s_shouldSweep = false;
        s_shouldAutoProgress = false;
    }
    // If shift-space is pressed, do (or finish) entire sweep
    else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS && (mods & GLFW_MOD_SHIFT)) {
        s_shouldSweep = true;
    }
    // If ctrl-space is pressed, will automatically move on to the next angle
    else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL)) {
        s_shouldAutoProgress = !s_shouldAutoProgress;
    }
    // If F is pressed, do fast sweep
    else if (key == GLFW_KEY_F && (action == GLFW_PRESS) && !mods) {
        if (Simulation::slice() == 0 && !s_shouldAutoProgress) {
            doFastSweep(s_angleOfAttack);
        }
    }
    // If Shift-F is pressed, do fast sweep of all angles
    else if (key == GLFW_KEY_F && action == GLFW_PRESS && mods == GLFW_MOD_SHIFT) {
        if (Simulation::slice() == 0 && !s_shouldAutoProgress) {
            doAllAngles();
        }
    }
    // If up arrow is pressed, increase angle of attack
    else if (key == GLFW_KEY_UP && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        if (Simulation::slice() == 0 && !s_shouldAutoProgress) {
            changeAngleOfAttack(k_angleOfAttackIncrement);
        }
    }
    // If down arrow is pressed, decrease angle of attack
    else if (key == GLFW_KEY_DOWN && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        if (Simulation::slice() == 0 && !s_shouldAutoProgress) {
            changeAngleOfAttack(-k_angleOfAttackIncrement);
        }
    }
    // If O key is pressed, increase rudder angle
    else if (key == GLFW_KEY_O && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        if (Simulation::slice() == 0) {
            changeRudderAngle(k_manualAngleIncrement);
        }
    }
    // If I key is pressed, decrease rudder angle
    else if (key == GLFW_KEY_I && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        if (Simulation::slice() == 0) {
            changeRudderAngle(-k_manualAngleIncrement);
        }
    }
    // If K key is pressed, increase elevator angle
    else if (key == GLFW_KEY_K && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        if (Simulation::slice() == 0) {
            changeElevatorAngle(k_manualAngleIncrement);
        }
    }
    // If J key is pressed, decrease elevator angle
    else if (key == GLFW_KEY_J && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        if (Simulation::slice() == 0) {
            changeElevatorAngle(-k_manualAngleIncrement);
        }
    }
    // If M key is pressed, increase aileron angle
    else if (key == GLFW_KEY_M && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        if (Simulation::slice() == 0) {
            changeAileronAngle(k_manualAngleIncrement);
        }
    }
    // If N key is pressed, decrease aileron angle
    else if (key == GLFW_KEY_N && (action == GLFW_PRESS || action == GLFW_REPEAT) && !mods) {
        if (Simulation::slice() == 0) {
            changeAileronAngle(-k_manualAngleIncrement);
        }
    }
}


static bool setupLocalShader(const std::string & resourcesDir) {
    std::string shadersDir(resourcesDir + "/shaders");

    s_phongProg = std::make_shared<Program>();
    s_phongProg->setVerbose(true);
    s_phongProg->setShaderNames(shadersDir + "/phong.vert", shadersDir + "/phong.frag");
    if (!s_phongProg->init()) {
        std::cerr << "Failed to initialize foil shader" << std::endl;
        return false;
    }
    s_phongProg->addUniform("u_projMat");
    s_phongProg->addUniform("u_viewMat");
    s_phongProg->addUniform("u_modelMat");
    s_phongProg->addUniform("u_normalMat");
    return true;
}

static bool setup() {
    // Setup GLFW
    glfwSetErrorCallback(errorCallback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    if (!(s_mainWindow = glfwCreateWindow(k_width, k_height, "FlightSim", nullptr, nullptr))) {
        std::cerr << "Failed to create window" << std::endl;
        return false;
    }
    glfwDefaultWindowHints();

    glfwMakeContextCurrent(s_mainWindow);

    glfwSetKeyCallback(s_mainWindow, keyCallback);
    glfwSwapInterval(0);

    // Setup GLAD
    if (!gladLoadGL()) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    // Setup model    
    switch (k_simModel) {
        case SimModel::airfoil: s_model = Model::load(s_resourceDir + "/models/0012.obj"  ); break;
        case SimModel::    f18: s_model = Model::load(s_resourceDir + "/models/f18.grl"   ); break;
        case SimModel:: sphere: s_model = Model::load(s_resourceDir + "/models/sphere.obj"); break;
    }
    if (!s_model) {
        std::cerr << "Failed to load model" << std::endl;
        return false;
    }

    s_pos = vec3(0);
    s_vel = vec3(0);
    
    // Setup simulation
    if (!Simulation::setup(s_resourceDir)) {
        std::cerr << "Failed to setup simulation" << std::endl;
        return false;
    }

    setupLocalShader(s_resourceDir);

    glfwMakeContextCurrent(s_mainWindow);
    glfwFocusWindow(s_mainWindow);

    return true;
}



static void cleanup() {
    Simulation::cleanup();
    glfwTerminate();
}

static void update() {
    doFastSweep(s_angleOfAttack);
    vec3 cumForce = -s_lift;// + s_drag;
    vec3 acc = cumForce / 10000.0f; //replace 1 with mass of model (i.e. f18)
    s_vel += acc;
    s_pos += s_vel;

}

static mat4 getPerspectiveMatrix() {
    mat4 P;
    float fov(3.14159f / 4.0f);
    float aspect;
    if (k_width < k_height) {
        aspect = float(k_height) / float(k_width);
    }
    else {
        aspect = float(k_width) / float(k_height);
    }

    return glm::perspective(fov, aspect, 0.01f, 1000.f);
}

static mat4 getViewMatrix() {
    vec3 modelMatPos = vec3(s_modelMat[3][0], s_modelMat[3][1], s_modelMat[3][2]);
    vec3 camPos = vec3(0, 0.5, -5.5);
    vec3 lookPos = modelMatPos + s_pos;
    vec3 viewVec = lookPos - camPos;
    vec3 right = glm::cross(viewVec, vec3(0, 1, 0));
    vec3 up = glm::cross(right, viewVec);
    return glm::lookAt(
        camPos,
        lookPos,
        up
    );
}

static void render() {

    glViewport(0, 0, k_width, k_height);
    glClearColor(0.3f, 0.7f, 0.8f, 1.0f);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    mat4 M, N, V, P;
    //todo rigth now s_model mat is translated some distance back in the set simulation
    M = glm::translate(s_modelMat, s_pos);
    N = glm::transpose(glm::inverse(M));
    P = getPerspectiveMatrix();
    V = getViewMatrix();
    s_phongProg->bind(); 
    glUniformMatrix4fv(s_phongProg->getUniform("u_projMat"), 1, GL_FALSE, reinterpret_cast<const float *>(&P));
    glUniformMatrix4fv(s_phongProg->getUniform("u_viewMat"), 1, GL_FALSE, reinterpret_cast<const float *>(&V));
    s_model->draw(M, N, s_phongProg->getUniform("u_modelMat"), s_phongProg->getUniform("u_normalMat"));
    s_phongProg->unbind();
    glfwSwapBuffers(s_mainWindow);

    //glfwMakeContextCurrent(s_mainWindow);
}



int main(int argc, char ** argv) {
    if (!processArgs(argc, argv)) {
        std::exit(-1);
    }

    if (!setup()) {
        std::cerr << "Failed setup" << std::endl;
        return -1;
    }

    int fps(0);
    double then(glfwGetTime());
    
    // Loop until the user closes the window.
    while (!glfwWindowShouldClose(s_mainWindow)) {
        // Poll for and process events.
        glfwPollEvents();

        update();

        render();

        ++fps;
        double now(glfwGetTime());
        if (now - then >= 1.0) {
            //std::cout << "fps: " << fps << std::endl;
            fps = 0;
            then = now;
        }
    }

    cleanup();

    return 0;
}
