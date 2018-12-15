#include <GL/glew.h>
#include <GL/glut.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <iostream>
#include <string>
#include <vector>

#include "Shader.h"
#include "Mesh.h"
#include "Renderer.h"
#include "MassSpringSolver.h"

// BIG TODO (for later): refactor code to avoid using global state, and more OOP
// G L O B A L S ///////////////////////////////////////////////////////////////////
// Window Size
static int g_window_width = 480, g_window_height = 480;

// Constants
static const float PI = glm::pi<float>();

// TODO: refactor to remove some shader globals
// Shader Files
static const char* const g_basic_vshader = "./shaders/basic.vshader";
static const char* const g_phong_fshader = "./shaders/phong.fshader";
static const char* const g_pick_fshader = "./shaders/pick.fshader";

// Shader Handles
static GLuint g_vshaderBasic, g_fshaderPhong, g_fshaderPick; // unlinked shaders
static PhongShader g_phongShader; // linked phong shader
static PickShader g_pickShader; // link pick shader

// Mesh
static Mesh g_clothMesh; // halfedge data structure
static mesh_data g_meshData; // pointers to data buffers

// Render Target
static render_target g_renderTarget; // vertex, normal, texutre, index

// Animation
static const int fps = 60;

// Mass Spring System
mass_spring_system* g_system;
MassSpringSolver* g_solver;

float g_temp1[3];
float g_temp2[3];
// System parameters
namespace SystemParam {
	static const int n = 29; // must be odd, n * n = n_vertices
	static const float h = 0.01f; // time step, smaller for better results
	static const float r = 4.0f / (n - 1); // spring rest legnth
	static const float k = 1.0f; // spring stiffness
	static const float m = 0.5f / (n * n); // point mass
	static const float a = 0.991f; // damping, close to 1.0
	static const float g = 10.0f * m; // gravitational force
}

// Scene matrices
static glm::mat4 g_ModelViewMatrix;
static glm::mat4 g_ProjectionMatrix;

// F U N C T I O N S //////////////////////////////////////////////////////////////
// state initialization
static void initGlutState(int, char**);
static void initGLState();

static void initShaders(); // Read, compile and link shaders
static void initCloth(); // Generate cloth mesh
static void initScene(); // Generate scene matrices

// glut callbacks
static void display();
static void reshape(int, int);
//static void mouse(int, int, int, int);
//static void motion(int, int);
//static void keyboard(unsigned char, int, int);

// draw cloth function
static void drawCloth(bool picking);
static void animateCloth(int value);

// scene update
static void updateProjection();
static void updateRenderTarget();

// cleaning
static void deleteShaders();
//static void deleteBuffers();

// error checks
void checkGlErrors();

// M A I N //////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv) {
	try {
		initGlutState(argc, argv);
		glewInit();
		initGLState();

		initShaders();
		initCloth();
		initScene();

		glutTimerFunc((1.0f / fps) * 1000, animateCloth, 0);
		glutMainLoop();

		deleteShaders();
		return 0;
	}
	catch (const std::runtime_error& e) {
		std::cout << "Exception caught: " << e.what() << std::endl;
		return -1;
	}
}


// S T A T E  I N I T I A L I Z A T O N /////////////////////////////////////////////
static void initGlutState(int argc, char** argv) {
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
	glutInitWindowSize(g_window_width, g_window_height);
	glutCreateWindow("Cloth App");

	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	/*glutMotionFunc(motion);
	glutMouseFunc(mouse);
	glutKeyboardFunc(keyboard);*/
}

static void initGLState() {
	glClearColor(0.25f, 0.25f, 0.25f, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glEnable(GL_FRAMEBUFFER_SRGB);

	checkGlErrors();
}

static void initShaders() {
	std::ifstream ifBasic(g_basic_vshader);
	std::ifstream ifPhong(g_phong_fshader);
	std::ifstream ifPick(g_pick_fshader);

	g_vshaderBasic = glCreateShader(GL_VERTEX_SHADER);
	g_fshaderPhong = glCreateShader(GL_FRAGMENT_SHADER);
	g_fshaderPick = glCreateShader(GL_FRAGMENT_SHADER);

	if (!g_vshaderBasic || !g_fshaderPhong || !g_fshaderPick) {
		throw std::runtime_error("glCreateShader fails.");
	}

	compile_shader(g_vshaderBasic, ifBasic);
	compile_shader(g_fshaderPhong, ifPhong);
	compile_shader(g_fshaderPick, ifPick);

	g_phongShader = glCreateProgram();
	g_pickShader = glCreateProgram();

	if (!g_phongShader || !g_pickShader) {
		throw std::runtime_error("glCreateProgram fails.");
	}

	link_shader(g_phongShader, g_vshaderBasic, g_fshaderPhong);
	link_shader(g_pickShader, g_vshaderBasic, g_fshaderPick);

	g_phongShader.h_aPosition = glGetAttribLocation(g_phongShader, "aPosition");
	g_phongShader.h_aNormal = glGetAttribLocation(g_phongShader, "aNormal");
	g_phongShader.h_uModelViewMatrix = glGetUniformLocation(g_phongShader, "uModelViewMatrix");
	g_phongShader.h_uProjectionMatrix = glGetUniformLocation(g_phongShader, "uProjectionMatrix");

	g_pickShader.h_aPosition = glGetAttribLocation(g_pickShader, "aPosition");
	g_pickShader.h_aTexCoord = glGetAttribLocation(g_pickShader, "aTexCoord");
	g_pickShader.h_uTessFact = glGetUniformLocation(g_pickShader, "uTessFact");
	g_pickShader.h_uModelViewMatrix = glGetUniformLocation(g_pickShader, "uModelViewMatrix");
	g_pickShader.h_uProjectionMatrix = glGetUniformLocation(g_pickShader, "uProjectionMatrix");
	
	checkGlErrors();
}

static void initCloth() {
	// generate buffers
	glGenBuffers(1, &g_renderTarget.vbo);
	glGenBuffers(1, &g_renderTarget.nbo);
	glGenBuffers(1, &g_renderTarget.tbo);
	glGenBuffers(1, &g_renderTarget.ibo);


	// generate mesh
	const int n = SystemParam::n;
	MeshBuilder::buildGridNxN(g_clothMesh, n);

	// build index buffer
	g_meshData.ibuffLen = 6 * (n - 1) * (n - 1);
	g_meshData.ibuff = new unsigned int[g_meshData.ibuffLen];
	
	MeshBuilder::buildGridIBuffNxN(g_meshData.ibuff, n);

	// extract data buffers
	g_meshData.vbuffLen = g_meshData.nbuffLen = n * n * 3;
	g_meshData.tbuffLen = n * n * 2;

	g_meshData.vbuff = VERTEX_DATA(g_clothMesh);
	g_meshData.nbuff = NORMAL_DATA(g_clothMesh);
	g_meshData.tbuff = TEXTURE_DATA(g_clothMesh);

	// fill render target
	glBindBuffer(GL_ARRAY_BUFFER, g_renderTarget.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * g_meshData.vbuffLen,
		g_meshData.vbuff, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, g_renderTarget.nbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * g_meshData.nbuffLen,
		g_meshData.nbuff, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, g_renderTarget.tbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * g_meshData.tbuffLen,
		g_meshData.tbuff, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, g_renderTarget.ibo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(unsigned int) * g_meshData.ibuffLen, 
		g_meshData.ibuff, GL_STATIC_DRAW);

	checkGlErrors();

	// initialize mass spring system
	g_system = MassSpringBuilder::UniformGrid(
		SystemParam::n,
		SystemParam::h,
		SystemParam::r,
		SystemParam::k,
		SystemParam::m,
		SystemParam::a,
		SystemParam::g
	);

	// initialize mass spring solver
	g_solver = new MassSpringSolver(g_system, g_meshData.vbuff);

	// temp fixed points
	g_temp1[0] = g_meshData.vbuff[0];
	g_temp1[1] = g_meshData.vbuff[1];
	g_temp1[2] = g_meshData.vbuff[2];

	g_temp2[0] = g_meshData.vbuff[(SystemParam::n - 1) * 3 + 0];
	g_temp2[1] = g_meshData.vbuff[(SystemParam::n - 1) * 3 + 1];
	g_temp2[2] = g_meshData.vbuff[(SystemParam::n - 1) * 3 + 2];
	
}

static void initScene() {
	g_ModelViewMatrix = glm::lookAt(
		glm::vec3(7.0f, -10.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, -1.0f),
		glm::vec3(0.0f, 0.0f, 1.0f)
	) * glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.0f, 2.0f));
	updateProjection();
}

// G L U T  C A L L B A C K S //////////////////////////////////////////////////////
static void display() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	drawCloth(false);
	glutSwapBuffers();
}

static void reshape(int w, int h) {
	g_window_width = w;
	g_window_height = h;
	glViewport(0, 0, w, h);
	updateProjection();
	glutPostRedisplay();
}

// C L O T H ///////////////////////////////////////////////////////////////////////
static void drawCloth(bool picking) {
	
	if (picking) {
		PickShadingRenderer picker(&g_pickShader);
		picker.setModelview(g_ModelViewMatrix);
		picker.setProjection(g_ProjectionMatrix);
		picker.setTessFact(SystemParam::n);
		picker.setRenderTarget(g_renderTarget);
		picker.draw(g_meshData.ibuffLen);
	}
	else {
		PhongShadingRenderer phonger(&g_phongShader);
		phonger.setModelview(g_ModelViewMatrix);
		phonger.setProjection(g_ProjectionMatrix);
		phonger.setRenderTarget(g_renderTarget);
		phonger.draw(g_meshData.ibuffLen);
	}

}

static void animateCloth(int value) {
	// solve system
	g_solver->solve(5);

	// fix two points
	g_meshData.vbuff[0] = g_temp1[0];
	g_meshData.vbuff[1] = g_temp1[1];
	g_meshData.vbuff[2] = g_temp1[2];

	g_meshData.vbuff[(SystemParam::n - 1) * 3 + 0] = g_temp2[0];
	g_meshData.vbuff[(SystemParam::n - 1) * 3 + 1] = g_temp2[1];
	g_meshData.vbuff[(SystemParam::n - 1) * 3 + 2] = g_temp2[2];
	// update normals
	g_clothMesh.request_face_normals();
	g_clothMesh.update_normals();
	g_clothMesh.release_face_normals();

	// update target
	updateRenderTarget();

	// redisplay
	glutPostRedisplay();

	// reset timer
	glutTimerFunc((1.0f / fps) * 1000, animateCloth, 0);
}

// S C E N E  U P D A T E ///////////////////////////////////////////////////////////
static void updateProjection() {
	g_ProjectionMatrix = glm::perspective(PI / 4.0f,
		g_window_width * 1.0f / g_window_height, 0.01f, 1000.0f);
}

static void updateRenderTarget() {
	// update vertex positions
	glBindBuffer(GL_ARRAY_BUFFER, g_renderTarget.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * g_meshData.vbuffLen,
		g_meshData.vbuff, GL_STATIC_DRAW);

	// update vertex normals
	glBindBuffer(GL_ARRAY_BUFFER, g_renderTarget.nbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * g_meshData.nbuffLen,
		g_meshData.nbuff, GL_STATIC_DRAW);
}

// C L E A N  U P //////////////////////////////////////////////////////////////////
static void deleteShaders() {
	glDeleteShader(g_vshaderBasic);
	glDeleteShader(g_fshaderPhong);
	glDeleteShader(g_fshaderPick);

	glDeleteProgram(g_phongShader);
	glDeleteProgram(g_pickShader);

	checkGlErrors();
}

void checkGlErrors() {
	const GLenum errCode = glGetError();

	if (errCode != GL_NO_ERROR) {
		std::string error("GL Error: ");
		error += reinterpret_cast<const char*>(gluErrorString(errCode));
		std::cerr << error << std::endl;
		throw std::runtime_error(error);
	}
}