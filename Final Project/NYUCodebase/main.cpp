#ifdef _WINDOWS
#include <GL/glew.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_image.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <windows.h> 
#include <fstream>
#include <string>
#include <iostream>
#include <sstream>
#include "ShaderProgram.h"
#include "Matrix.h"
#include "stb_image.h"
#include <SDL_mixer.h>

using namespace std;

#ifdef _WINDOWS
#define RESOURCE_FOLDER ""
#else
#define RESOURCE_FOLDER "NYUCodebase.app/Contents/Resources/"
#endif

SDL_Window* displayWindow;

#define FIXED_TIMESTEP 0.0166666f
#define MAX_TIMESTEPS 6

enum GameState { MAIN_MENU, STAGE_1, STAGE_2, STAGE_3, STAGE_4, WIN_SCREEN, LOSE_SCREEN };
enum Tiles { LEFT, MIDDLE, RIGHT, BOTH, BLOCK };
enum SoundLoc { JUMP, WALK_1, WALK_2, PORTAL, THROW, STOMP, HIT, SCREECH, DEATH };
enum SpriteType { PLAYER_SPRITE, MIRROR_SPRITE, GHOST_SPRITE, BOSS_SPRITE, LEFT_TILE, MIDDLE_TILE, RIGHT_TILE, BOTH_TILE, BLOCK_TILE, WARP_TILE, OBJECT_TILE, ROCK_TILE, ROCK_INV, ROCK_PLAYER, ROCK_ENEMY };

// It's all the same
float texCoords[] = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f };

class Vector3 {
public:
	Vector3() {}
	Vector3(float x1, float y1, float z1) : x(x1), y(y1), z(z1) {}

	float x;
	float y;
	float z;

	float length() const {
		return sqrt(x*x + y*y + z*z);
	}

	void normalize() {
		x = x / length();
		y = y / length();
		z = z / length();
	}

	Vector3 operator*(const Matrix& v) {
		Vector3 v1;
		v1.x = v.m[0][0] * x + v.m[1][0] * y + v.m[2][0] * z + v.m[3][0] * 1;
		v1.y = v.m[0][1] * x + v.m[1][1] * y + v.m[2][1] * z + v.m[3][1] * 1;
		v1.z = v.m[0][2] * x + v.m[1][2] * y + v.m[2][2] * z + v.m[3][2] * 1;
		return v1;
	}
};

GLuint LoadTexture(const char *filePath) {
	int w, h, comp;
	unsigned char* image = stbi_load(filePath, &w, &h, &comp, STBI_rgb_alpha);

	if (image == NULL) {
		std::cout << "Unable to load image. Make sure to path is correct\n";
		assert(false);
	}

	GLuint retTexture;
	glGenTextures(1, &retTexture);
	glBindTexture(GL_TEXTURE_2D, retTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	stbi_image_free(image);
	return retTexture;
}

class SheetSprite {
public:
	SheetSprite() {}
	SheetSprite(unsigned int texID, float u1, float v1, float width1, float height1, float size1) : textureID(texID), u(u1), v(v1), width(width1), height(height1), size(size1) {}

	float size;
	unsigned int textureID;
	float u;
	float v;
	float width;
	float height;

	void SheetSprite::Draw(ShaderProgram* program) {
		glBindTexture(GL_TEXTURE_2D, textureID);
		GLfloat texCoords[] = {
			u, v + height,
			u + width, v,
			u, v,
			u + width, v,
			u, v + height,
			u + width, v + height
		};
		float aspect = width / height;
		float vertices[] = {
			-0.5f * size * aspect, -0.5f * size,
			0.5f * size * aspect, 0.5f * size,
			-0.5f * size * aspect, 0.5f * size,
			0.5f * size * aspect, 0.5f * size,
			-0.5f * size * aspect, -0.5f * size ,
			0.5f * size * aspect, -0.5f * size };

		glUseProgram(program->programID);
		glBindTexture(GL_TEXTURE_2D, textureID);
		glVertexAttribPointer(program->positionAttribute, 2, GL_FLOAT, false, 0, vertices);
		glEnableVertexAttribArray(program->positionAttribute);
		glVertexAttribPointer(program->texCoordAttribute, 2, GL_FLOAT, false, 0, texCoords);
		glEnableVertexAttribArray(program->texCoordAttribute);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glDisableVertexAttribArray(program->positionAttribute);
		glDisableVertexAttribArray(program->texCoordAttribute);
	}
};

class Entity {
public:
	Entity() {}
	Entity(float x, float y, SheetSprite sprite1, int t) : position(x, y, 0.0f), sprite(sprite1), type(t) {}

	Vector3 position;
	Vector3 velocity = Vector3(1.0f, 1.0f, 1.0f);
	Vector3 size = Vector3(1.0f, 1.0f, 1.0f);
	bool left = false;
	int HP = 1;
	int type;

	bool collideUp;
	bool collideDown;
	bool collideLeft;
	bool collideRight;

	SheetSprite sprite;

	void render(Matrix& modelMatrix, ShaderProgram& program) {
		modelMatrix.identity();
		modelMatrix.Translate(position.x, position.y, position.z);
		if (left)
			modelMatrix.Scale(-size.x, size.y, size.z);
		else
			modelMatrix.Scale(size.x, size.y, size.z);
		program.setModelMatrix(modelMatrix);
		sprite.Draw(&program);
	}
};

class Background {
public:
	Background() {}
	Background(float x, float y, float z, GLuint sprite1) : position(x, y, z), sprite(sprite1) {
	}

	Vector3 position;

	GLuint sprite;

	void render(Matrix& modelMatrix, ShaderProgram& program) {
		glBindTexture(GL_TEXTURE_2D, sprite);
		modelMatrix.identity();
		program.setModelMatrix(modelMatrix);

		float aVertices[] = { -position.x, -position.y, position.x, -position.y, position.x, position.y, -position.x, -position.y, position.x, position.y, -position.x, position.y };
		glVertexAttribPointer(program.positionAttribute, 2, GL_FLOAT, false, 0, aVertices);
		glEnableVertexAttribArray(program.positionAttribute);

		glVertexAttribPointer(program.texCoordAttribute, 2, GL_FLOAT, false, 0, texCoords);
		glEnableVertexAttribArray(program.texCoordAttribute);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		glDisableVertexAttribArray(program.positionAttribute);
		glDisableVertexAttribArray(program.texCoordAttribute);
	}
};

void DrawText(ShaderProgram* program, int fontTexture, std::string text, float size, float spacing) {
	float texture_size = 1.0 / 16.0f;
	std::vector<float> vertexData;
	std::vector<float> texCoordData;

	for (size_t i = 0; i < text.size(); i++) {
		int spriteIndex = (int)text[i];

		float texture_x = (float)(spriteIndex % 16) / 16.0f;
		float texture_y = (float)(spriteIndex / 16) / 16.0f;

		vertexData.insert(vertexData.end(), {
			((size + spacing) * i) + (-0.5f * size), 0.5f * size,
			((size + spacing) * i) + (-0.5f * size), -0.5f * size,
			((size + spacing) * i) + (0.5f * size), 0.5f * size,
			((size + spacing) * i) + (0.5f * size), -0.5f * size,
			((size + spacing) * i) + (0.5f * size), 0.5f * size,
			((size + spacing) * i) + (-0.5f * size), -0.5f * size,
		});
		texCoordData.insert(texCoordData.end(), {
			texture_x, texture_y,
			texture_x, texture_y + texture_size,
			texture_x + texture_size, texture_y,
			texture_x + texture_size, texture_y + texture_size,
			texture_x + texture_size, texture_y,
			texture_x, texture_y + texture_size,
		});
	}

	glUseProgram(program->programID);
	glBindTexture(GL_TEXTURE_2D, fontTexture);
	glVertexAttribPointer(program->positionAttribute, 2, GL_FLOAT, false, 0, vertexData.data());
	glEnableVertexAttribArray(program->positionAttribute);
	glVertexAttribPointer(program->texCoordAttribute, 2, GL_FLOAT, false, 0, texCoordData.data());
	glEnableVertexAttribArray(program->texCoordAttribute);
	glDrawArrays(GL_TRIANGLES, 0, text.size() * 6);
	glDisableVertexAttribArray(program->positionAttribute);
	glDisableVertexAttribArray(program->texCoordAttribute);
}

int state = 0;
Entity player;
Entity portal;
Mix_Music *music;
vector<SheetSprite> playerSprite;
vector<SheetSprite> portalSprite;
vector<SheetSprite> rockSprite;
vector<SheetSprite> mirrorSprite;
vector<SheetSprite> ghostSprite;
vector<SheetSprite> bossSprite;
vector<SheetSprite> bgFloor; // Floor tiles
vector<Background> background;
vector<Entity> gameObj; // Objects randomly put in map6
vector<Entity> gameFloor; // Floor of stage
vector<Entity> enemies;
vector<int> enemyAnim;
vector<float> enemyDelay;
vector<Entity> rocks;
vector<Mix_Chunk> sounds;
int portalAnim = 0;
bool menuSelect = true;
bool walk = true;
bool telePortal = false;
int ghostAttack = 0;
float ghostMoved;
float lastMoved = 0.0f;
float portalDelay = 0.0f;
float portalTime = 0.0f;
float gravity = 5.0f;
float y_distance = 0.0f;
float bossRun = 1.0f;
float dropRocks = 0.0f;
float lastThrown = 0.0f;
float freeRocks = 0.0f;
int rocksThrown = 0;

// Enable Keyboard Inputs
const Uint8* keys = SDL_GetKeyboardState(NULL);

void setCollideFalse(Entity& entity) {
	entity.collideUp = false;
	entity.collideDown = false;
	entity.collideLeft = false;
	entity.collideRight = false;
}

// Highly unoptimnized, look at W4C1
void LoadSprites() {
	// Player
	GLuint playerTexture = LoadTexture("Textures/players.png");
	playerSprite.push_back(SheetSprite(playerTexture, 246.0f / 512.0f, 44.0f / 512.0f, 38.0f / 512.0f, 50.0f / 512.0f, 0.3f));
	playerSprite.push_back(SheetSprite(playerTexture, 282.0f / 512.0f, 345.0f / 512.0f, 38.0f / 512.0f, 48.0f / 512.0f, 0.3f));
	playerSprite.push_back(SheetSprite(playerTexture, 246.0f / 512.0f, 446.0f / 512.0f, 38.0f / 512.0f, 50.0f / 512.0f, 0.3f));

	// Portal
	GLuint portalTexture = LoadTexture("Textures/portal.png");
	portalSprite.push_back(SheetSprite(portalTexture, 0.0f / 840.0f, 0.0f / 457.0f, 209.0f / 840.0f, 228.0f / 457.0f, 0.8f));
	portalSprite.push_back(SheetSprite(portalTexture, 210.0f / 840.0f, 0.0f / 457.0f, 209.0f / 840.0f, 228.0f / 457.0f, 0.8f));
	portalSprite.push_back(SheetSprite(portalTexture, 0.0f / 840.0f, 229.0f / 457.0f, 209.0f / 840.0f, 228.0f / 457.0f, 0.8f));
	portalSprite.push_back(SheetSprite(portalTexture, 210.0f / 840.0f, 229.0f / 457.0f, 209.0f / 840.0f, 228.0f / 457.0f, 0.8f));
	portalSprite.push_back(SheetSprite(portalTexture, 420.0f / 840.0f, 0.0f / 457.0f, 209.0f / 840.0f, 228.0f / 457.0f, 0.8f));
	portalSprite.push_back(SheetSprite(portalTexture, 630.0f / 840.0f, 0.0f / 457.0f, 209.0f / 840.0f, 228.0f / 457.0f, 0.8f));
	portalSprite.push_back(SheetSprite(portalTexture, 420.0f / 840.0f, 229.0f / 457.0f, 209.0f / 840.0f, 228.0f / 457.0f, 0.8f));
	portalSprite.push_back(SheetSprite(portalTexture, 630.0f / 840.0f, 229.0f / 457.0f, 209.0f / 840.0f, 228.0f / 457.0f, 0.8f));

	// Tiles
	GLuint tileTexture = LoadTexture("Textures/tiles.png");
	bgFloor.push_back(SheetSprite(tileTexture, 325.0f / 512.0f, 882.0f / 1024.0f, 64.0f / 512.0f, 64.0f / 1024.0f, 0.3f));
	bgFloor.push_back(SheetSprite(tileTexture, 390.0f / 512.0f, 520.0f / 1024.0f, 64.0f / 512.0f, 64.0f / 1024.0f, 0.3f));
	bgFloor.push_back(SheetSprite(tileTexture, 325.0f / 512.0f, 817.0f / 1024.0f, 64.0f / 512.0f, 64.0f / 1024.0f, 0.3f));
	bgFloor.push_back(SheetSprite(tileTexture, 390.0f / 512.0f, 392.0f / 1024.0f, 64.0f / 512.0f, 64.0f / 1024.0f, 0.3f));
	bgFloor.push_back(SheetSprite(tileTexture, 390.0f / 512.0f, 650.0f / 1024.0f, 64.0f / 512.0f, 64.0f / 1024.0f, 0.3f));

	// Rocks
	GLuint rockTexture = LoadTexture("Textures/rocks.png");
	rockSprite.push_back(SheetSprite(rockTexture, 15.0f / 1024.0f, 15.0f / 1024.0f, 98.0f / 1024.0f, 98.0f / 1024.0f, 0.3f));
	rockSprite.push_back(SheetSprite(rockTexture, 15.0f / 1024.0f, 143.0f / 1024.0f, 98.0f / 1024.0f, 98.0f / 1024.0f, 0.3f));
	rockSprite.push_back(SheetSprite(rockTexture, 15.0f / 1024.0f, 271.0f / 1024.0f, 98.0f / 1024.0f, 98.0f / 1024.0f, 0.3f));
	rockSprite.push_back(SheetSprite(rockTexture, 15.0f / 1024.0f, 399.0f / 1024.0f, 98.0f / 1024.0f, 98.0f / 1024.0f, 0.3f));
	rockSprite.push_back(SheetSprite(rockTexture, 15.0f / 1024.0f, 527.0f / 1024.0f, 98.0f / 1024.0f, 98.0f / 1024.0f, 0.3f));
	rockSprite.push_back(SheetSprite(rockTexture, 15.0f / 1024.0f, 655.0f / 1024.0f, 98.0f / 1024.0f, 98.0f / 1024.0f, 0.3f));
	rockSprite.push_back(SheetSprite(rockTexture, 15.0f / 1024.0f, 783.0f / 1024.0f, 98.0f / 1024.0f, 98.0f / 1024.0f, 0.3f));
	rockSprite.push_back(SheetSprite(rockTexture, 15.0f / 1024.0f, 911.0f / 1024.0f, 98.0f / 1024.0f, 98.0f / 1024.0f, 0.3f));

	// Enemy: Mirror
	GLuint mirrorTexture = LoadTexture("Textures/mirror.png");
	mirrorSprite.push_back(SheetSprite(mirrorTexture, 0.0f / 336.0f, 0.0f / 213.0f, 83.0f / 336.0f, 106.0f / 213.0f, 1.5f));
	mirrorSprite.push_back(SheetSprite(mirrorTexture, 84.0f / 336.0f, 0.0f / 213.0f, 83.0f / 336.0f, 106.0f / 213.0f, 1.5f));
	mirrorSprite.push_back(SheetSprite(mirrorTexture, 168.0f / 336.0f, 0.0f / 213.0f, 83.0f / 336.0f, 106.0f / 213.0f, 1.5f));
	mirrorSprite.push_back(SheetSprite(mirrorTexture, 0.0f / 336.0f, 107.0f / 213.0f, 83.0f / 336.0f, 106.0f / 213.0f, 1.5f));
	mirrorSprite.push_back(SheetSprite(mirrorTexture, 84.0f / 336.0f, 107.0f / 213.0f, 83.0f / 336.0f, 106.0f / 213.0f, 1.5f));
	mirrorSprite.push_back(SheetSprite(mirrorTexture, 168.0f / 336.0f, 107.0f / 213.0f, 83.0f / 336.0f, 106.0f / 213.0f, 1.5f));
	mirrorSprite.push_back(SheetSprite(mirrorTexture, 252.0f / 336.0f, 0.0f / 213.0f, 83.0f / 336.0f, 106.0f / 213.0f, 1.5f));

	// Enemy: Ghost
	GLuint ghostTexture = LoadTexture("Textures/ghost.png");
	ghostSprite.push_back(SheetSprite(ghostTexture, 0.0f / 416.0f, 0.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 0.0f / 416.0f, 124.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 104.0f / 416.0f, 124.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 208.0f / 416.0f, 0.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 312.0f / 416.0f, 0.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 208.0f / 416.0f, 124.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 312.0f / 416.0f, 124.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 104.0f / 416.0f, 248.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 104.0f / 416.0f, 0.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));
	ghostSprite.push_back(SheetSprite(ghostTexture, 0.0f / 416.0f, 248.0f / 371.0f, 103.0f / 416.0f, 123.0f / 371.0f, 2.0f));

	// Enemy: Ghost
	GLuint bossTexture = LoadTexture("Textures/boss.png");
	bossSprite.push_back(SheetSprite(bossTexture, 0.0f / 258.0f, 0.0f / 160.0f, 128.0f / 258.0f, 160.0f / 160.0f, 17.0f));
	bossSprite.push_back(SheetSprite(bossTexture, 129.0f / 258.0f, 0.0f / 160.0f, 128.0f / 258.0f, 160.0f / 160.0f, 17.0f));

	// Background
	GLuint bg1 = LoadTexture("Textures/bg.jpg");
	background.push_back(Background(-5.0f, 3.0f, 0.0f, bg1));
}

void LoadMusic() {
	Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096);
	music = Mix_LoadMUS("Sound/bgmusic.mp3");
	Mix_PlayMusic(music, -1);

	sounds.push_back(*Mix_LoadWAV("Sound/playerjump.wav"));
	sounds.push_back(*Mix_LoadWAV("Sound/walk1.wav"));
	sounds.push_back(*Mix_LoadWAV("Sound/walk2.wav"));
	sounds.push_back(*Mix_LoadWAV("Sound/portal.wav"));
	sounds.push_back(*Mix_LoadWAV("Sound/throw.wav"));
	sounds.push_back(*Mix_LoadWAV("Sound/stomp.wav"));
	sounds.push_back(*Mix_LoadWAV("Sound/hit.wav"));
	sounds.push_back(*Mix_LoadWAV("Sound/screech.wav"));
	sounds.push_back(*Mix_LoadWAV("Sound/death.wav"));
}

void ClearStage() {
	gameFloor.clear();
	gameObj.clear();
	enemies.clear();
	rocks.clear();
	enemyAnim.clear();
	enemyDelay.clear();
	y_distance = 0.0f;
	bossRun = 1.0f;
}

void LoadStage1() {

	ClearStage();

	player = Entity(0.0f, -1.0f, playerSprite[0], PLAYER_SPRITE);
	portal = Entity(12.0f, -0.8f, portalSprite[0], WARP_TILE);

	gameFloor.push_back(Entity(0.0f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 0; i < 10; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(6.6f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	gameFloor.push_back(Entity(7.8f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 13; i < 16; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(10.2f, -1.01f, bgFloor[BOTH], BOTH_TILE));
	gameFloor.push_back(Entity(10.2f, -1.3f, bgFloor[BLOCK], BLOCK_TILE));
	for (int i = 17; i < 19; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(12.0f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	// Arrow Keys
	GLuint arrowTexture = LoadTexture("Textures/arrowKeys.png");
	SheetSprite arrowSprite = SheetSprite(arrowTexture, 0.0f, 0.0f, 1.0f, 227.0f / 332.0f, 0.8f);
	gameObj.push_back(Entity(1.0f, -0.75f, arrowSprite, OBJECT_TILE));
}

void LoadStage2() {

	ClearStage();

	player = Entity(0.0f, -1.0f, playerSprite[0], PLAYER_SPRITE);
	enemies.push_back(Entity(18.0f, -0.45f, mirrorSprite[0], MIRROR_SPRITE));
	enemies[enemies.size() - 1].left = true;
	enemyAnim.push_back(0);
	enemyDelay.push_back(0);
	portal = Entity(19.8f, -0.8f, portalSprite[0], WARP_TILE); //need to change once everything is done
	gameObj.push_back(Entity(14.0f, -0.8f, rockSprite[rand() % 8], ROCK_TILE));

	//First block
	gameFloor.push_back(Entity(0.0f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 0; i < 1; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(1.2f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	//Second block
	gameFloor.push_back(Entity(2.4f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 4; i < 6; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(4.2f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	//Third block, elevated
	gameFloor.push_back(Entity(5.4f, -1.01f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 9; i < 11; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.01f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(7.2f, -1.01f, bgFloor[RIGHT], RIGHT_TILE));

	//Fourth block, elevated higher than the third block
	gameFloor.push_back(Entity(7.8f, -0.72f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 13; i < 15; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -0.72f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(9.6f, -0.72f, bgFloor[RIGHT], RIGHT_TILE));

	//Fifth block, elevated higher than the fourth block
	gameFloor.push_back(Entity(10.2f, -0.43f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 17; i < 18; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -0.43f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(11.4f, -0.43f, bgFloor[RIGHT], RIGHT_TILE));

	//Sixth block, same height as first block, has a rock
	gameFloor.push_back(Entity(12.0f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 20; i < 24; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(15.0f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	//Seventh block, with the portal and the enemy
	gameFloor.push_back(Entity(16.8f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 28; i < 32; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(19.8f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	// Spacebar
	GLuint spaceTexture = LoadTexture("Textures/spacebar.png");
	SheetSprite spaceSprite = SheetSprite(spaceTexture, 0.0f, 0.0f, 1.0f, 527.0f / 2048.0f, 0.4f);
	gameObj.push_back(Entity(18.0f, 0.35f, spaceSprite, OBJECT_TILE));
}

void LoadStage3() {

	ClearStage();

	//rocks.push_back(Entity(-3.35f, 1.8f, rockSprite[rand() % 7], ROCK_INV));
	//rocks.push_back(Entity(-3.05f, 1.8f, rockSprite[rand() % 7], ROCK_INV));
	player = Entity(0.0f, -1.0f, playerSprite[0], PLAYER_SPRITE);
	portal = Entity(19.8f, -0.8f, portalSprite[0], WARP_TILE);
	enemies.push_back(Entity(18.0f, -13.7f, ghostSprite[0], GHOST_SPRITE));
	enemies[enemies.size() - 1].HP = 5;
	enemyAnim.push_back(0);
	enemyDelay.push_back(0);

	//First tile
	gameFloor.push_back(Entity(0.0f, -1.3f, bgFloor[BOTH], BOTH_TILE));

	//HUGEEEE block
	gameFloor.push_back(Entity(0.6f, -15.0f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 1; i < 32; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -15.0f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(19.8f, -15.0f, bgFloor[RIGHT], RIGHT_TILE));
}

void LoadStage4() {
	//Hard Level
	ClearStage();

	enemies.push_back(Entity(-10.0f, -2.0f, bossSprite[0], BOSS_SPRITE));
	enemyAnim.push_back(0);
	enemyDelay.push_back(0);
	player = Entity(0.0f, -1.0f, playerSprite[0], PLAYER_SPRITE);
	portal = Entity(60.0f, -4.5f, portalSprite[0], WARP_TILE);

	//First block
	gameFloor.push_back(Entity(0.0f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 0; i < 1; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(1.2f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	//Second block
	gameFloor.push_back(Entity(2.4f, -1.01f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 4; i < 6; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.01f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(4.2f, -1.01f, bgFloor[RIGHT], RIGHT_TILE));

	//Really High block
	gameFloor.push_back(Entity(3.6f, -0.43f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 6; i < 9; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -0.43f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(6.0f, -0.43f, bgFloor[RIGHT], RIGHT_TILE));

	//Third block
	gameFloor.push_back(Entity(6.0f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 10; i < 11; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(7.2f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	//Fourth block
	gameFloor.push_back(Entity(8.4f, -1.01f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 14; i < 15; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.01f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(9.6f, -1.01f, bgFloor[RIGHT], RIGHT_TILE));

	//Fifth block
	gameFloor.push_back(Entity(10.2f, -0.43f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 17; i < 19; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -0.43f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(12.0f, -0.43f, bgFloor[RIGHT], RIGHT_TILE));

	//Sixth Block
	gameFloor.push_back(Entity(12.6f, 0.15f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 21; i < 24; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), 0.15f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(15.0f, 0.15f, bgFloor[RIGHT], RIGHT_TILE));

	//Seventh block
	gameFloor.push_back(Entity(16.2f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 27; i < 32; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(19.8f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	//Eight block
	gameFloor.push_back(Entity(21.6f, -1.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 36; i < 40; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(24.6f, -1.3f, bgFloor[RIGHT], RIGHT_TILE));

	//Ninth block
	gameFloor.push_back(Entity(25.8f, -1.01f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 43; i < 44; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -1.01f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(27.0f, -1.01f, bgFloor[RIGHT], RIGHT_TILE));

	//Tenth block
	gameFloor.push_back(Entity(28.2f, -0.43f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 47; i < 48; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -0.43f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(29.4f, -0.43f, bgFloor[RIGHT], RIGHT_TILE));

	//Eleventh block
	gameFloor.push_back(Entity(30.0f, 0.15f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 50; i < 51; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), 0.15f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(31.2f, 0.15f, bgFloor[RIGHT], RIGHT_TILE));

	//Twelfth block
	gameFloor.push_back(Entity(32.4f, 0.46f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 54; i < 56; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), 0.46f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(34.2f, 0.46f, bgFloor[RIGHT], RIGHT_TILE));

	//Thirteenth block
	gameFloor.push_back(Entity(35.4f, 0.75f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 59; i < 60; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), 0.75f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(36.6f, 0.75f, bgFloor[RIGHT], RIGHT_TILE));

	//Fourteenth block
	gameFloor.push_back(Entity(37.8f, 0.75f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 63; i < 64; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), 0.75f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(39.0f, 0.75f, bgFloor[RIGHT], RIGHT_TILE));

	//Fifteenth block
	gameFloor.push_back(Entity(40.2f, 1.04f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 67; i < 68; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), 1.04f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(41.4f, 1.04f, bgFloor[RIGHT], RIGHT_TILE));

	//16th block
	gameFloor.push_back(Entity(42.0f, -5.3f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 70; i < 73; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -5.3f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(44.4f, -5.3f, bgFloor[RIGHT], RIGHT_TILE));

	//17th block
	gameFloor.push_back(Entity(45.6f, -5.01f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 76; i < 77; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -5.01f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(46.8f, -5.01f, bgFloor[RIGHT], RIGHT_TILE));

	//18th block
	gameFloor.push_back(Entity(48.0f, -4.43f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 80; i < 81; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -4.43f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(49.2f, -4.43f, bgFloor[RIGHT], RIGHT_TILE));

	//19th block
	gameFloor.push_back(Entity(50.4f, -3.85f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 84; i < 86; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -3.85f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(52.2f, -3.85f, bgFloor[RIGHT], RIGHT_TILE));

	//20th block
	gameFloor.push_back(Entity(53.4f, -5.01f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 89; i < 90; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -5.01f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(54.6f, -5.01f, bgFloor[RIGHT], RIGHT_TILE));

	//21th block
	gameFloor.push_back(Entity(55.8f, -4.43f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 93; i < 94; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -4.43f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(57.0f, -4.43f, bgFloor[RIGHT], RIGHT_TILE));

	//22th block
	gameFloor.push_back(Entity(58.2f, -5.01f, bgFloor[LEFT], LEFT_TILE));
	for (int i = 97; i < 99; i++) {
		gameFloor.push_back(Entity(0.6f*(i + 1), -5.01f, bgFloor[MIDDLE], MIDDLE_TILE));
	}
	gameFloor.push_back(Entity(60.0f, -5.01f, bgFloor[RIGHT], RIGHT_TILE));
}

void RenderBackground(Matrix& modelMatrix, ShaderProgram& program) {
	//Draws the parallax background
	for (size_t i = 0; i < background.size(); i++)
		background[i].render(modelMatrix, program);
}

void RenderMainMenu(Matrix& modelMatrix, ShaderProgram& program, GLuint fontSheet) {
	RenderBackground(modelMatrix, program);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, 0.5f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "ONCE FALLEN", 0.15f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, -0.3f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "START", 0.1f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, -0.5f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "QUIT", 0.1f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	if (menuSelect)
		modelMatrix.Translate(-1.35f, -0.29f, 0.0f);
	else
		modelMatrix.Translate(-1.35f, -0.49f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, ">", 0.2f, 0.0f);
}

void RenderStage(Matrix& modelMatrix, ShaderProgram& program) {
	RenderBackground(modelMatrix, program);

	portal.render(modelMatrix, program);

	for (size_t i = 0; i < gameFloor.size(); i++)
		gameFloor[i].render(modelMatrix, program);

	for (size_t i = 0; i < enemies.size(); i++)
		enemies[i].render(modelMatrix, program);

	for (size_t i = 0; i < gameObj.size(); i++)
		gameObj[i].render(modelMatrix, program);

	for (size_t i = 0; i < rocks.size(); i++)
		rocks[i].render(modelMatrix, program);

	player.render(modelMatrix, program);
}

void RenderLose(Matrix& modelMatrix, ShaderProgram& program, GLuint fontSheet) {
	RenderBackground(modelMatrix, program);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, 0.5f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "GAME OVER", 0.15f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, -0.3f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "MAIN MENU", 0.1f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, -0.5f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "QUIT", 0.1f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	if (menuSelect)
		modelMatrix.Translate(-1.35f, -0.29f, 0.0f);
	else
		modelMatrix.Translate(-1.35f, -0.49f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, ">", 0.2f, 0.0f);
}

void RenderWin(Matrix& modelMatrix, ShaderProgram& program, GLuint fontSheet) {
	RenderBackground(modelMatrix, program);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, 0.5f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "CONGRATULATIONS", 0.15f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, -0.3f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "MAIN MENU", 0.1f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	modelMatrix.Translate(-1.25f, -0.5f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, "QUIT", 0.1f, 0.0f);

	modelMatrix.identity();
	modelMatrix.Scale(2.0f, 2.0f, 0.0f);
	if (menuSelect)
		modelMatrix.Translate(-1.35f, -0.29f, 0.0f);
	else
		modelMatrix.Translate(-1.35f, -0.49f, 0.0f);
	program.setModelMatrix(modelMatrix);
	DrawText(&program, fontSheet, ">", 0.2f, 0.0f);
}

void UpdateMovement(float elapsed) {
	if (!telePortal) {
		// Enemy movement ghost
		for (size_t i = 0; i < enemies.size(); i++) {
			if (enemies[i].type == GHOST_SPRITE) {
				if (rand() % 100 == 0)
					enemies[i].velocity.x *= -1.0f;
				if (rand() % 100 == 0)
					enemies[i].velocity.y *= -1.0f;

				if (rand() % 1000 == 0 && ghostAttack == 0 && y_distance < 15.0f) {
					Mix_PlayChannel(-1, &sounds[SCREECH], 0);
					ghostAttack = rand() % 2 + 1;
				}

				if (ghostAttack == 0) {
					if (enemies[i].position.x + 3.35f < player.position.x)
						enemies[i].position.x += fabs(enemies[i].velocity.x) * elapsed;
					else if (enemies[i].position.x - 3.75f > player.position.x)
						enemies[i].position.x -= fabs(enemies[i].velocity.x) * elapsed;
					else
						enemies[i].position.x -= enemies[i].velocity.x * elapsed;

					if (enemies[i].position.y + 1.0f < player.position.y)
						enemies[i].position.y += fabs(enemies[i].velocity.y) * elapsed;
					else if (enemies[i].position.y - 3.0f > player.position.y)
						enemies[i].position.y -= fabs(enemies[i].velocity.y) * elapsed;
					else
						enemies[i].position.y -= enemies[i].velocity.y * elapsed;
				}

				if (enemies[i].position.x < player.position.x)
					enemies[i].left = false;
				else
					enemies[i].left = true;
			}
		}

		// Player animation XOR
		if ((keys[SDL_SCANCODE_RIGHT] && player.collideDown) ^ (keys[SDL_SCANCODE_LEFT] && player.collideDown)) {
			if (keys[SDL_SCANCODE_RIGHT]) {
				if (lastMoved > 0.3f) {
					Mix_PlayChannel(-1, &sounds[WALK_1], 0);
					player.sprite = playerSprite[walk];
					lastMoved = 0.0f;
					walk = !walk;
					player.left = false;
				}
			}
			if (keys[SDL_SCANCODE_LEFT]) {
				if (lastMoved > 0.3f) {
					Mix_PlayChannel(-1, &sounds[WALK_2], 0);
					player.sprite = playerSprite[walk];
					lastMoved = 0.0f;
					walk = !walk;
					player.left = true;
				}
			}
		}
		// Jump animation
		else if (keys[SDL_SCANCODE_UP])
			player.sprite = playerSprite[2];
		// Reset player animation
		else if (player.collideDown) {
			player.sprite = playerSprite[0];
			walk = true;
		}

		// Stop y axis movement if colliding
		if ((player.collideDown && player.velocity.y < 0.0f) || (player.collideUp && player.velocity.y > 0.0f))
			player.velocity.y = 0.0f;
		else { // or keep doing what you were doing
			y_distance -= player.velocity.y * elapsed;
			for (size_t i = 0; i < gameFloor.size(); i++)
				gameFloor[i].position.y -= player.velocity.y * elapsed;
			for (size_t i = 0; i < gameObj.size(); i++)
				gameObj[i].position.y -= player.velocity.y * elapsed;
			for (size_t i = 0; i < enemies.size(); i++)
				enemies[i].position.y -= player.velocity.y * elapsed;
			portal.position.y -= player.velocity.y * elapsed;

			player.velocity.y -= gravity * elapsed;
		}

		// Jump
		if (keys[SDL_SCANCODE_UP] && player.collideDown) {
			Mix_PlayChannel(-1, &sounds[JUMP], 0);
			player.velocity.y = 2.0f;
		}

		// Thrown rock
		for (size_t i = 0; i < gameObj.size(); i++) {
			if (gameObj[i].type == ROCK_PLAYER) {
				if (gameObj[i].left)
					gameObj[i].position.x -= gameObj[i].velocity.x * elapsed;
				else
					gameObj[i].position.x += gameObj[i].velocity.x * elapsed;
			}
			if (gameObj[i].type == ROCK_ENEMY) {
				gameObj[i].position.y += gameObj[i].velocity.y * elapsed;
				gameObj[i].position.x += gameObj[i].velocity.x * elapsed;
			}
			if (gameObj[i].type == ROCK_TILE && !gameObj[i].collideDown) {
				gameObj[i].position.y -= gameObj[i].velocity.y * elapsed;
			}
		}

		// Player movement XOR
		if (keys[SDL_SCANCODE_RIGHT]) {
			if (!player.collideLeft) { // Pressing right moves the entities left!
				for (size_t i = 0; i < gameFloor.size(); i++)
					gameFloor[i].position.x -= player.velocity.x * elapsed;
				for (size_t i = 0; i < gameObj.size(); i++)
					gameObj[i].position.x -= player.velocity.x * elapsed;
				for (size_t i = 0; i < enemies.size(); i++)
					enemies[i].position.x -= player.velocity.x * elapsed;
				portal.position.x -= player.velocity.x * elapsed;
			}
		}
		else if (keys[SDL_SCANCODE_LEFT]) {
			if (!player.collideRight) { // Pressing left moves the entities right!
				for (size_t i = 0; i < gameFloor.size(); i++)
					gameFloor[i].position.x += player.velocity.x * elapsed;
				for (size_t i = 0; i < gameObj.size(); i++)
					gameObj[i].position.x += player.velocity.x * elapsed;
				for (size_t i = 0; i < enemies.size(); i++)
					enemies[i].position.x += player.velocity.x * elapsed;
				portal.position.x += player.velocity.x * elapsed;
			}
		}
		else {
			player.velocity.x = 1.0f;
		}
		if (player.velocity.x < 2.0f && player.collideDown)
			player.velocity.x += elapsed;
	}
}

void UpdateGameplay(float elapsed) {
	// Animate the portal horribly
	portal.sprite = portalSprite[portalAnim];
	if (portalDelay > 0.1f) {
		portalAnim++;
		portalDelay = 0.0f;
	}
	if (portalAnim == 8)
		portalAnim = 0;
	portalDelay += elapsed;

	// Animate enemies horribly
	for (size_t i = 0; i < enemies.size(); i++) {
		if (enemies[i].type == MIRROR_SPRITE)
			enemies[i].sprite = mirrorSprite[enemyAnim[i]];
		else if (enemies[i].type == GHOST_SPRITE)
			enemies[i].sprite = ghostSprite[enemyAnim[i]];
		else if (enemies[i].type == BOSS_SPRITE)
			enemies[i].sprite = bossSprite[enemyAnim[i]];

		if (enemies[i].type == GHOST_SPRITE) {
			if (ghostAttack == 1) {
				if (ghostMoved > 1.0f) {
					ghostAttack = 3;
					ghostMoved = 0.0f;
				}
			}
			else if (ghostAttack == 2) {
				if (ghostMoved > 0.5f) {
					ghostMoved = 0.0f;
					gameObj.push_back(Entity(enemies[i].position.x, enemies[i].position.y, rockSprite[rand() % 8], ROCK_ENEMY));
					if (rand() % 2 == 0)
						gameObj[gameObj.size() - 1].velocity = Vector3(float(rand() % 5 + 1), float(rand() % 3 + 1), 0.0f);
					else
						gameObj[gameObj.size() - 1].velocity = Vector3(-float(rand() % 5 + 1), float(-rand() % 3 + 1), 0.0f);
					rocksThrown++;
				}
				if (rocksThrown == 10) {
					rocksThrown = 0;
					ghostAttack = 0;
				}
			}
			else if (ghostAttack == 3) {
				if (ghostMoved > 1.0f) {
					ghostAttack = 0;
					ghostMoved = 0.0f;
				}
				else {
					enemies[i].position.x -= (enemies[i].position.x - player.position.x) * elapsed;
					enemies[i].position.y -= (enemies[i].position.y - player.position.y) * elapsed;
				}
			}
		}

		// Animation Delay not tied to FPS
		if (enemies[i].type == BOSS_SPRITE) {
			if (enemyDelay[i] > bossRun) {
				Mix_PlayChannel(-1, &sounds[STOMP], 0);
				enemyAnim[i]++;
				enemyDelay[i] = 0.0f;
				enemies[i].position.x += 30.0f * elapsed;
				if (bossRun > 0.0f)
					bossRun -= 3.0f * elapsed;
			}
		}
		else {
			if (enemyDelay[i] > 0.1f) {
				enemyAnim[i]++;
				enemyDelay[i] = 0.0f;
			}
		}

		// Animation reset
		if (enemyAnim[i] >= 6 && enemies[i].type == MIRROR_SPRITE)
			enemyAnim[i] = 0;
		else if (enemies[i].type == GHOST_SPRITE) {
			if (ghostAttack > 0 && (enemyAnim[i] > 8 || enemyAnim[i] < 7))
				enemyAnim[i] = 7;
			else if (!ghostAttack > 0 && enemyAnim[i] > 6)
				enemyAnim[i] = 0;
		}
		else if (enemyAnim[i] >= 2 && enemies[i].type == BOSS_SPRITE)
			enemyAnim[i] = 0;

		enemyDelay[i] += elapsed;
	}
	ghostMoved += elapsed;

	// Throw a rock
	if (keys[SDL_SCANCODE_SPACE] && rocks.size() > 0 && lastThrown > 0.5f) {
		Mix_PlayChannel(-1, &sounds[THROW], 0);
		gameObj.push_back(rocks[rocks.size() - 1]);
		if (player.left) {
			gameObj[gameObj.size() - 1].position.x = player.position.x - player.sprite.width * 3;
			gameObj[gameObj.size() - 1].left = true;
		}
		else
			gameObj[gameObj.size() - 1].position.x = player.position.x + player.sprite.width * 3;
		gameObj[gameObj.size() - 1].position.y = player.position.y;
		gameObj[gameObj.size() - 1].type = ROCK_PLAYER;
		rocks.pop_back();

		lastThrown = 0.0f;
	}
	lastThrown += elapsed;

	// Reset
	setCollideFalse(player);
	for (size_t i = 0; i < gameObj.size(); i++) {
		if (gameObj[i].type == ROCK_TILE) {
			setCollideFalse(gameObj[i]);
		}
	}

	// Map Collision handling
	for (size_t i = 0; i < gameFloor.size(); i++) {
		if (player.position.x - player.sprite.width < gameFloor[i].position.x + gameFloor[i].sprite.width * 2.8 &&
			player.position.x + player.sprite.width > gameFloor[i].position.x - gameFloor[i].sprite.width * 2.8 &&
			player.position.y - player.sprite.height < gameFloor[i].position.y + gameFloor[i].sprite.height * 3 &&
			player.position.y + player.sprite.height > gameFloor[i].position.y - gameFloor[i].sprite.height * 3) {
			if (player.position.y > gameFloor[i].position.y)
				player.collideDown = true;
			else if (player.position.y < gameFloor[i].position.y)
				player.collideUp = true;
			if (player.position.x > gameFloor[i].position.x + gameFloor[i].sprite.width * 3.0 && (gameFloor[i].type == RIGHT_TILE || gameFloor[i].type == BOTH_TILE))
				player.collideRight = true;
			else if (player.position.x < gameFloor[i].position.x - gameFloor[i].sprite.width * 3.0 && (gameFloor[i].type == LEFT_TILE || gameFloor[i].type == BOTH_TILE))
				player.collideLeft = true;
		}

		// Map Rock Collision handling
		for (size_t j = 0; j < gameObj.size(); j++) {
			if (gameObj[j].position.x - gameObj[j].sprite.width < gameFloor[i].position.x + gameFloor[i].sprite.width * 2 &&
				gameObj[j].position.x + gameObj[j].sprite.width > gameFloor[i].position.x - gameFloor[i].sprite.width * 2 &&
				gameObj[j].position.y - gameObj[j].sprite.height < gameFloor[i].position.y + gameFloor[i].sprite.height * 2 &&
				gameObj[j].position.y + gameObj[j].sprite.height > gameFloor[i].position.y - gameFloor[i].sprite.height * 2) {
				if (gameObj[j].position.y > gameFloor[i].position.y)
					gameObj[j].collideDown = true;
				if (gameObj[j].position.y < gameFloor[i].position.y)
					gameObj[j].collideUp = true;
			}
		}
	}

	// Enemy Collision handling
	for (size_t i = 0; i < enemies.size(); i++) {
		if (enemies[i].type == BOSS_SPRITE) {
			if (player.position.x - player.sprite.width < enemies[i].position.x + enemies[i].sprite.width * 2 &&
				player.position.x + player.sprite.width > enemies[i].position.x - enemies[i].sprite.width * 2) {
				Mix_PlayChannel(-1, &sounds[DEATH], 0);
				state = LOSE_SCREEN;
			}
		}
		else if (player.position.x - player.sprite.width < enemies[i].position.x + enemies[i].sprite.width * 2 &&
			player.position.x + player.sprite.width > enemies[i].position.x - enemies[i].sprite.width * 2 &&
			player.position.y - player.sprite.height < enemies[i].position.y + enemies[i].sprite.height * 2 &&
			player.position.y + player.sprite.height > enemies[i].position.y - enemies[i].sprite.height * 2) {
			Mix_PlayChannel(-1, &sounds[DEATH], 0);
			state = LOSE_SCREEN;
		}
	}

	// Rock Collision handling
	for (size_t i = 0; i < gameObj.size(); i++) {
		/*if (gameObj[i].position.y < -21.0f) {
		std::swap(gameObj[i], gameObj[gameObj.size() - 1]);
		gameObj.pop_back();
		i--;
		}*/
		if (gameObj[i].type == ROCK_PLAYER) {
			// Enemy rock collision
			for (size_t j = 0; j < enemies.size(); j++) {
				if (gameObj[i].position.x - gameObj[i].sprite.width < enemies[j].position.x + enemies[j].sprite.width * 2 &&
					gameObj[i].position.x + gameObj[i].sprite.width > enemies[j].position.x - enemies[j].sprite.width * 2 &&
					gameObj[i].position.y - gameObj[i].sprite.height < enemies[j].position.y + enemies[j].sprite.height * 2 &&
					gameObj[i].position.y + gameObj[i].sprite.height > enemies[j].position.y - enemies[j].sprite.height * 2) {
					if (enemies[j].type == BOSS_SPRITE) {
						Mix_PlayChannel(-1, &sounds[HIT], 0);
						if (bossRun < 1.0f)
							bossRun += 0.15f;
						std::swap(gameObj[i], gameObj[gameObj.size() - 1]);
						gameObj.pop_back();
						i--;
					}
					else {
						Mix_PlayChannel(-1, &sounds[HIT], 0);
						enemies[j].HP--;

						if (enemies[j].type == GHOST_SPRITE) {
							if (enemies[j].HP == 0) {
								portal.position.x = enemies[j].position.x;
								portal.position.y = player.position.y + 0.1f;
							}
						}
						if (enemies[j].HP == 0) {
							std::swap(enemies[j], enemies[enemies.size() - 1]);
							enemies.pop_back();
							if (enemies.size() > 1) {
								j--;
							}
						}
						std::swap(gameObj[i], gameObj[gameObj.size() - 1]);
						gameObj.pop_back();
						i--;
					}
				}
			}
		}
		else if (gameObj[i].type == ROCK_TILE && rocks.size() <= 5) {
			if (player.position.x - player.sprite.width < gameObj[i].position.x + gameObj[i].sprite.width &&
				player.position.x + player.sprite.width > gameObj[i].position.x - gameObj[i].sprite.width &&
				player.position.y - player.sprite.height < gameObj[i].position.y + gameObj[i].sprite.height * 2 &&
				player.position.y + player.sprite.height > gameObj[i].position.y - gameObj[i].sprite.height * 2) {
				rocks.push_back(gameObj[i]);
				rocks[rocks.size() - 1].position.x = -3.35f + (rocks.size() - 1) * 0.3f;
				rocks[rocks.size() - 1].position.y = 1.8f;
				rocks[rocks.size() - 1].velocity.x = 3.0f;
				std::swap(gameObj[i], gameObj[gameObj.size() - 1]);
				gameObj.pop_back();
				i--;
			}
		}
		else if (gameObj[i].type == ROCK_ENEMY) {
			if (player.position.x - player.sprite.width < gameObj[i].position.x + gameObj[i].sprite.width &&
				player.position.x + player.sprite.width > gameObj[i].position.x - gameObj[i].sprite.width &&
				player.position.y - player.sprite.height < gameObj[i].position.y + gameObj[i].sprite.height * 2 &&
				player.position.y + player.sprite.height > gameObj[i].position.y - gameObj[i].sprite.height * 2) {
				Mix_PlayChannel(-1, &sounds[DEATH], 0);
				state = LOSE_SCREEN;
			}
		}
	}

	// Portal Collision handling
	if (player.position.x - player.sprite.width < portal.position.x + portal.sprite.width / 2 &&
		player.position.x + player.sprite.width > portal.position.x - portal.sprite.width / 2 &&
		player.position.y - player.sprite.height < portal.position.y + portal.sprite.height / 2 &&
		player.position.y + player.sprite.height > portal.position.y - portal.sprite.height / 2) {
		if (keys[SDL_SCANCODE_UP]) {
			Mix_PlayChannel(-1, &sounds[PORTAL], 0);
			telePortal = true;
		}
	}

	if (telePortal) {
		portalTime += elapsed;
		player.sprite = playerSprite[2];
		if (player.size.x > 0.0f) {
			player.size.x -= 0.4f * elapsed;
			player.size.y -= 0.4f * elapsed;
		}
	}

	UpdateMovement(elapsed);
}

bool UpdateMainMenu() {
	if (lastMoved > 0.2f) {
		if (keys[SDL_SCANCODE_SPACE]) {
			if (menuSelect) {
				LoadStage1();
				state = STAGE_1;
				lastMoved = 0.0f;
			}
			else
				return false;
		}
		else if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_UP]) {
			menuSelect = !menuSelect;
			lastMoved = 0.0f;
		}
	}
	return true;
}

void UpdateStage1(float elapsed) {
	UpdateGameplay(elapsed);

	if (portalTime >= 3.0) {
		LoadStage2();
		state = STAGE_2;
		telePortal = false;
		portalTime = 0.0f;
	}
}

void UpdateStage2(float elapsed) {
	UpdateGameplay(elapsed);

	if (portalTime >= 3.0) {
		LoadStage3();
		state = STAGE_3;
		telePortal = false;
		portalTime = 0.0f;
	}
}

void UpdateStage3(float elapsed) {
	UpdateGameplay(elapsed);

	if (portalTime >= 3.0) {
		LoadStage4();
		state = STAGE_4;
		telePortal = false;
		portalTime = 0.0f;
	}

	if (freeRocks > 5.0f) {
		freeRocks = 0.0f;
		rocks.push_back(Entity(-3.35f + (rocks.size() - 1) * 0.3f, 1.8f, rockSprite[rand() % 8], ROCK_INV));
		rocks[rocks.size() - 1].velocity.x = 3.0f;
	}
	freeRocks += elapsed;
}

void UpdateStage4(float elapsed, Matrix viewMatrix) {
	UpdateGameplay(elapsed);

	if (portalTime >= 3.0) {
		state = WIN_SCREEN;
		telePortal = false;
		portalTime = 0.0f;
	}

	if (rand() % 100 > 98) {
		gameObj.push_back(Entity(float(rand() % 105 - 35) / 10.0f, 2.2f, rockSprite[rand() % 8], ROCK_TILE));
	}
	dropRocks += elapsed;
}

bool UpdateGameOverMenu() {
	if (lastMoved > 0.2f) {
		if (keys[SDL_SCANCODE_SPACE]) {
			if (menuSelect) {
				state = MAIN_MENU;
				lastMoved = 0.0f;
			}
			else
				return false;
		}
		else if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_UP]) {
			menuSelect = !menuSelect;
			lastMoved = 0.0f;
		}
	}

	return true;
}

int main(int argc, char *argv[]) {
	SDL_Init(SDL_INIT_VIDEO);
	displayWindow = SDL_CreateWindow("Once Fallen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 360, SDL_WINDOW_OPENGL);
	SDL_GLContext context = SDL_GL_CreateContext(displayWindow);
	SDL_GL_MakeCurrent(displayWindow, context);
#ifdef _WINDOWS
	glewInit();
#endif

	// Setup
	glViewport(0, 0, 640, 360);

	ShaderProgram program(RESOURCE_FOLDER"vertex_textured.glsl", RESOURCE_FOLDER"fragment_textured.glsl");

	Matrix projectionMatrix;
	Matrix modelMatrix;
	Matrix viewMatrix;

	projectionMatrix.setOrthoProjection(-3.55f, 3.55f, -2.0f, 2.0f, -1.0f, 1.0f);

	glUseProgram(program.programID);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float lastFrameTicks = 0.0f;

	LoadSprites();
	LoadMusic();

	// Font
	GLuint fontSheet = LoadTexture("Textures/font1.png");

	SDL_Event event;
	bool done = false;
	while (!done) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT || event.type == SDL_WINDOWEVENT_CLOSE) {
				done = true;
			}
		}

		if (y_distance > 20.0f && state == STAGE_3) {
			Mix_PlayChannel(-1, &sounds[DEATH], 0);
			state = LOSE_SCREEN;
			y_distance = 0.0f;
		}
		else if (y_distance > 5.0f && state != STAGE_3) {
			Mix_PlayChannel(-1, &sounds[DEATH], 0);
			state = LOSE_SCREEN;
			y_distance = 0.0f;
		}

		// Time
		float ticks = (float)SDL_GetTicks() / 1000.0f;
		float elapsed = ticks - lastFrameTicks;
		lastFrameTicks = ticks;
		/*float fixedElapsed = elapsed;
		if (fixedElapsed > FIXED_TIMESTEP * MAX_TIMESTEPS) {
			fixedElapsed = FIXED_TIMESTEP * MAX_TIMESTEPS;
		}
		while (fixedElapsed >= FIXED_TIMESTEP) {
			fixedElapsed -= FIXED_TIMESTEP;
			// Update state
			switch (state) {
			case MAIN_MENU:
				if (!UpdateMainMenu())
					done = true;
				break;
			case STAGE_1:
				UpdateStage1(FIXED_TIMESTEP);
				break;
			case STAGE_2:
				UpdateStage2(FIXED_TIMESTEP);
				break;
			case STAGE_3:
				UpdateStage3(FIXED_TIMESTEP);
				break;
			case STAGE_4:
				UpdateStage4(FIXED_TIMESTEP, viewMatrix);
				break;
			case LOSE_SCREEN:
				if (!UpdateGameOverMenu())
					done = true;
				break;
			case WIN_SCREEN:
				if (!UpdateGameOverMenu())
					done = true;
				break;
			}
		}*/

		lastMoved += elapsed;
		// End Time

		// Drawing
		glClear(GL_COLOR_BUFFER_BIT);

		program.setModelMatrix(modelMatrix);
		program.setProjectionMatrix(projectionMatrix);
		program.setViewMatrix(viewMatrix);

		if (keys[SDL_SCANCODE_Q])
			done == true;

		// Update state
		switch (state) {
		case MAIN_MENU:
			if (!UpdateMainMenu())
				done = true;
			break;
		case STAGE_1:
			UpdateStage1(elapsed);
			break;
		case STAGE_2:
			UpdateStage2(elapsed);
			break;
		case STAGE_3:
			UpdateStage3(elapsed);
			break;
		case STAGE_4:
			UpdateStage4(elapsed, viewMatrix);
			break;
		case LOSE_SCREEN:
			if (!UpdateGameOverMenu())
				done = true;
			break;
		case WIN_SCREEN:
			if (!UpdateGameOverMenu())
				done = true;
			break;
		}

		// Render state
		switch (state) {
		case MAIN_MENU:
			RenderMainMenu(modelMatrix, program, fontSheet);
			break;
		case STAGE_1:
			RenderStage(modelMatrix, program);
			break;
		case STAGE_2:
			RenderStage(modelMatrix, program);
			break;
		case STAGE_3:
			RenderStage(modelMatrix, program);
			break;
		case STAGE_4:
			RenderStage(modelMatrix, program);
			break;
		case LOSE_SCREEN:
			RenderLose(modelMatrix, program, fontSheet);
			break;
		case WIN_SCREEN:
			RenderWin(modelMatrix, program, fontSheet);
			break;
		}

		SDL_GL_SwapWindow(displayWindow);
	}

	Mix_FreeMusic(music);
	for (size_t i = 0; i < sounds.size(); i++) {
		Mix_FreeChunk(&sounds[i]);
	}
	SDL_Quit();
	return 0;
}