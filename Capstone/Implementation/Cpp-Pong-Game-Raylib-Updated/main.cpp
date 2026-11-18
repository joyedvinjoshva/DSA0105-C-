#include <raylib.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

// =============================================================================
// MODULE 3: AUDIO & USER INTERFACE (Audio System & Colors)
// =============================================================================
const Color COLOR_BG         = Color{ 24, 28, 36, 255 };     // Deep Slate Navy
const Color COLOR_COURT_R    = Color{ 32, 38, 48, 255 };     // Right Court Half
const Color COLOR_ACCENT     = Color{ 46, 204, 113, 255 };   // Vibrant Emerald Green
const Color COLOR_BALL       = Color{ 241, 196, 15, 255 };   // Bright Warm Yellow
const Color COLOR_PADDLE_P1  = Color{ 52, 152, 219, 255 };   // Bright Sky Blue
const Color COLOR_PADDLE_P2  = Color{ 231, 76, 60, 255 };    // Coral Red
const Color COLOR_LINE       = Color{ 255, 255, 255, 50 };   // Subtle Translucent White

// Procedural sound synthesizer: generates 8-bit retro sound without external files
class SoundEngine {
public:
    Sound sndHit;
    Sound sndWall;
    Sound sndScore;
    bool ready;

    void Init() {
        InitAudioDevice();
        ready = IsAudioDeviceReady();
        if (ready) {
            sndHit   = GenerateTone(659.25f, 0.08f, 0.7f); // High E (Paddle hit)
            sndWall  = GenerateTone(329.63f, 0.06f, 0.5f); // Low E (Wall bounce)
            sndScore = GenerateTone(880.00f, 0.25f, 0.8f); // A5 celebration tone
        }
    }

    void PlayHit()   { if (ready) PlaySound(sndHit); }
    void PlayWall()  { if (ready) PlaySound(sndWall); }
    void PlayScore() { if (ready) PlaySound(sndScore); }

    void Cleanup() {
        if (ready) {
            UnloadSound(sndHit);
            UnloadSound(sndWall);
            UnloadSound(sndScore);
            CloseAudioDevice();
        }
    }

private:
    Sound GenerateTone(float freq, float duration, float volume) {
        int sampleRate = 44100;
        int frameCount = (int)(sampleRate * duration);
        short* samples = (short*)malloc(frameCount * sizeof(short));

        for (int i = 0; i < frameCount; i++) {
            float t = (float)i / (float)sampleRate;
            float envelope = 1.0f - ((float)i / (float)frameCount); // Smooth decay
            samples[i] = (short)(sinf(2.0f * PI * freq * t) * (30000.0f * volume) * envelope);
        }

        Wave wave = { 0 };
        wave.frameCount = (unsigned int)frameCount;
        wave.sampleRate = (unsigned int)sampleRate;
        wave.sampleSize = 16;
        wave.channels   = 1;
        wave.data       = samples;

        Sound sound = LoadSoundFromWave(wave);
        free(samples);
        return sound;
    }
};

// =============================================================================
// MODULE 1: ENTITY & PHYSICS ENGINE
// =============================================================================
const int TRAIL_COUNT = 6;

class Ball {
public:
    float x, y;
    float speed_x, speed_y;
    int radius;
    float baseSpeed;
    Vector2 trail[TRAIL_COUNT];

    void Init(int screenWidth, int screenHeight) {
        radius = 12;
        baseSpeed = 7.0f;
        Reset(screenWidth, screenHeight);
        for (int i = 0; i < TRAIL_COUNT; i++) {
            trail[i] = { x, y };
        }
    }

    void Reset(int screenWidth, int screenHeight) {
        x = (float)screenWidth / 2.0f;
        y = (float)screenHeight / 2.0f;

        int dirX = (GetRandomValue(0, 1) == 0) ? -1 : 1;
        int dirY = (GetRandomValue(0, 1) == 0) ? -1 : 1;

        speed_x = baseSpeed * dirX;
        speed_y = (float)GetRandomValue(3, 6) * dirY;
    }

    void UpdateTrail() {
        for (int i = TRAIL_COUNT - 1; i > 0; i--) {
            trail[i] = trail[i - 1];
        }
        trail[0] = { x, y };
    }

    void Draw() {
        // Draw trailing motion effect
        for (int i = 0; i < TRAIL_COUNT; i++) {
            float ratio = 1.0f - ((float)i / TRAIL_COUNT);
            Color trailColor = ColorAlpha(COLOR_BALL, ratio * 0.4f);
            DrawCircleV(trail[i], radius * (0.85f * ratio), trailColor);
        }
        // Draw main ball
        DrawCircle((int)x, (int)y, (float)radius, COLOR_BALL);
    }
};

class Paddle {
public:
    float x, y;
    float width, height;
    float speed;
    Color color;

    void Init(float startX, float startY, Color paddleColor) {
        width  = 20.0f;
        height = 110.0f;
        x      = startX;
        y      = startY;
        speed  = 7.0f;
        color  = paddleColor;
    }

    void Clamp(int screenHeight) {
        if (y < 10) y = 10;
        if (y + height > screenHeight - 10) y = (float)screenHeight - 10 - height;
    }

    void Draw() {
        DrawRectangleRounded(Rectangle{ x, y, width, height }, 0.4f, 8, color);
    }
};

class CpuPaddle : public Paddle {
public:
    void UpdateAI(float ball_x, float ball_y, int screenWidth, int screenHeight) {
        // AI reacts when ball moves towards its side (left half)
        if (ball_x < (float)screenWidth * 0.75f) {
            float centerPaddle = y + height / 2.0f;
            float deadzone = 15.0f; // Human-like reaction deadzone

            if (centerPaddle < ball_y - deadzone) {
                y += speed;
            } else if (centerPaddle > ball_y + deadzone) {
                y -= speed;
            }
        }
        Clamp(screenHeight);
    }
};

// =============================================================================
// MODULE 2: GAME STATE & RULES CONTROLLER (GameManager)
// =============================================================================
enum GameState {
    STATE_MENU,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_GAME_OVER
};

class GameManager {
public:
    GameState state;
    bool isTwoPlayer;
    int p1Score;
    int p2Score;
    int maxScore;
    const char* winnerText;

    Ball ball;
    Paddle p1;        // Right paddle (Player 1 / Human)
    CpuPaddle cpu;    // Left paddle (CPU)
    Paddle p2;        // Left paddle (Player 2 in 2-player mode)
    SoundEngine audio;

    int screenWidth;
    int screenHeight;

    void Init(int width, int height) {
        screenWidth  = width;
        screenHeight = height;
        state        = STATE_MENU;
        isTwoPlayer  = false;
        p1Score      = 0;
        p2Score      = 0;
        maxScore     = 5;
        winnerText   = "";

        audio.Init();
        ball.Init(screenWidth, screenHeight);

        // Player 1 (Right)
        p1.Init(screenWidth - 35.0f, (screenHeight - 110.0f) / 2.0f, COLOR_PADDLE_P1);
        // Left Paddle (CPU or Player 2)
        cpu.Init(15.0f, (screenHeight - 110.0f) / 2.0f, COLOR_PADDLE_P2);
        p2.Init(15.0f, (screenHeight - 110.0f) / 2.0f, COLOR_PADDLE_P2);
    }

    void ResetMatch() {
        p1Score = 0;
        p2Score = 0;
        winnerText = "";
        ball.Reset(screenWidth, screenHeight);
        p1.y = (screenHeight - p1.height) / 2.0f;
        cpu.y = (screenHeight - cpu.height) / 2.0f;
        p2.y = (screenHeight - p2.height) / 2.0f;
    }

    void HandleInput() {
        if (state == STATE_MENU) {
            if (IsKeyPressed(KEY_SPACE)) {
                ResetMatch();
                state = STATE_PLAYING;
            }
            if (IsKeyPressed(KEY_M)) {
                isTwoPlayer = !isTwoPlayer;
            }
        }
        else if (state == STATE_PLAYING) {
            if (IsKeyPressed(KEY_P)) {
                state = STATE_PAUSED;
            }

            // Player 1 (Right): Up / Down Arrows
            if (IsKeyDown(KEY_UP))   p1.y -= p1.speed;
            if (IsKeyDown(KEY_DOWN)) p1.y += p1.speed;
            p1.Clamp(screenHeight);

            // Left Side: Either Player 2 (W/S) or AI
            if (isTwoPlayer) {
                if (IsKeyDown(KEY_W)) p2.y -= p2.speed;
                if (IsKeyDown(KEY_S)) p2.y += p2.speed;
                p2.Clamp(screenHeight);
            } else {
                cpu.UpdateAI(ball.x, ball.y, screenWidth, screenHeight);
            }
        }
        else if (state == STATE_PAUSED) {
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_SPACE)) {
                state = STATE_PLAYING;
            }
        }
        else if (state == STATE_GAME_OVER) {
            if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_SPACE)) {
                ResetMatch();
                state = STATE_PLAYING;
            }
            if (IsKeyPressed(KEY_M)) {
                state = STATE_MENU;
            }
        }
    }

    void UpdatePhysics() {
        if (state != STATE_PLAYING) return;

        ball.UpdateTrail();
        ball.x += ball.speed_x;
        ball.y += ball.speed_y;

        // Top & bottom wall bounce
        if (ball.y - ball.radius <= 0) {
            ball.y = (float)ball.radius;
            ball.speed_y *= -1.0f;
            audio.PlayWall();
        }
        if (ball.y + ball.radius >= screenHeight) {
            ball.y = (float)screenHeight - ball.radius;
            ball.speed_y *= -1.0f;
            audio.PlayWall();
        }

        // Active paddle on left side
        Paddle& leftPaddle = isTwoPlayer ? p2 : cpu;

        // Collision with Right Paddle (Player 1)
        Rectangle p1Rec = { p1.x, p1.y, p1.width, p1.height };
        if (CheckCollisionCircleRec({ ball.x, ball.y }, (float)ball.radius, p1Rec) && ball.speed_x > 0) {
            ball.x = p1.x - ball.radius; // Prevent ball sticking
            // Dynamic bounce angle based on where ball hits paddle
            float hitFactor = (ball.y - (p1.y + p1.height / 2.0f)) / (p1.height / 2.0f);
            ball.speed_x = -fabsf(ball.speed_x) * 1.05f; // Slight speed increment
            ball.speed_y = hitFactor * 8.0f;
            audio.PlayHit();
        }

        // Collision with Left Paddle (CPU / Player 2)
        Rectangle leftRec = { leftPaddle.x, leftPaddle.y, leftPaddle.width, leftPaddle.height };
        if (CheckCollisionCircleRec({ ball.x, ball.y }, (float)ball.radius, leftRec) && ball.speed_x < 0) {
            ball.x = leftPaddle.x + leftPaddle.width + ball.radius;
            float hitFactor = (ball.y - (leftPaddle.y + leftPaddle.height / 2.0f)) / (leftPaddle.height / 2.0f);
            ball.speed_x = fabsf(ball.speed_x) * 1.05f;
            ball.speed_y = hitFactor * 8.0f;
            audio.PlayHit();
        }

        // Scoring: Left side scores (Ball went past right)
        if (ball.x - ball.radius > screenWidth) {
            p2Score++;
            audio.PlayScore();
            if (p2Score >= maxScore) {
                winnerText = isTwoPlayer ? "PLAYER 2 WINS!" : "CPU WINS!";
                state = STATE_GAME_OVER;
            } else {
                ball.Reset(screenWidth, screenHeight);
            }
        }

        // Scoring: Right side scores (Ball went past left)
        if (ball.x + ball.radius < 0) {
            p1Score++;
            audio.PlayScore();
            if (p1Score >= maxScore) {
                winnerText = "PLAYER 1 WINS!";
                state = STATE_GAME_OVER;
            } else {
                ball.Reset(screenWidth, screenHeight);
            }
        }
    }

    void DrawCourt() {
        ClearBackground(COLOR_BG);

        // Subtle split court
        DrawRectangle(screenWidth / 2, 0, screenWidth / 2, screenHeight, COLOR_COURT_R);

        // Center divider line (dashed)
        for (int y = 10; y < screenHeight; y += 30) {
            DrawRectangle(screenWidth / 2 - 2, y, 4, 15, COLOR_LINE);
        }

        // Center decorative ring
        DrawCircleLines(screenWidth / 2, screenHeight / 2, 80, COLOR_LINE);

        // Scores
        const char* leftScoreTxt = TextFormat("%i", p2Score);
        const char* rightScoreTxt = TextFormat("%i", p1Score);
        DrawText(leftScoreTxt, screenWidth / 4 - MeasureText(leftScoreTxt, 70) / 2, 40, 70, COLOR_LINE);
        DrawText(rightScoreTxt, 3 * screenWidth / 4 - MeasureText(rightScoreTxt, 70) / 2, 40, 70, COLOR_LINE);

        // Mode and Player labels
        DrawText(isTwoPlayer ? "PLAYER 2 (W/S)" : "CPU", 40, 20, 20, COLOR_PADDLE_P2);
        DrawText("PLAYER 1 (ARROWS)", screenWidth - MeasureText("PLAYER 1 (ARROWS)", 20) - 40, 20, 20, COLOR_PADDLE_P1);
    }

    void DrawHUD() {
        if (state == STATE_MENU) {
            DrawRectangle(0, 0, screenWidth, screenHeight, ColorAlpha(BLACK, 0.75f));

            const char* title = "C++ RAYLIB PONG";
            DrawText(title, screenWidth / 2 - MeasureText(title, 50) / 2, screenHeight / 2 - 120, 50, COLOR_ACCENT);

            const char* modeText = isTwoPlayer ? "Current Mode: [ 2-PLAYER PVP ]" : "Current Mode: [ 1-PLAYER VS CPU ]";
            DrawText(modeText, screenWidth / 2 - MeasureText(modeText, 24) / 2, screenHeight / 2 - 40, 24, RAYWHITE);

            const char* sub1 = "Press [ SPACE ] to Start Match";
            DrawText(sub1, screenWidth / 2 - MeasureText(sub1, 22) / 2, screenHeight / 2 + 20, 22, COLOR_BALL);

            const char* sub2 = "Press [ M ] to Toggle 1P / 2P Mode";
            DrawText(sub2, screenWidth / 2 - MeasureText(sub2, 20) / 2, screenHeight / 2 + 60, 20, LIGHTGRAY);

            const char* sub3 = "First to 5 points wins!";
            DrawText(sub3, screenWidth / 2 - MeasureText(sub3, 18) / 2, screenHeight / 2 + 100, 18, GRAY);
        }
        else if (state == STATE_PAUSED) {
            DrawRectangle(0, 0, screenWidth, screenHeight, ColorAlpha(BLACK, 0.6f));
            const char* pausedTxt = "GAME PAUSED";
            DrawText(pausedTxt, screenWidth / 2 - MeasureText(pausedTxt, 48) / 2, screenHeight / 2 - 30, 48, WHITE);

            const char* resumeTxt = "Press [ P ] or [ SPACE ] to Resume";
            DrawText(resumeTxt, screenWidth / 2 - MeasureText(resumeTxt, 20) / 2, screenHeight / 2 + 30, 20, LIGHTGRAY);
        }
        else if (state == STATE_GAME_OVER) {
            DrawRectangle(0, 0, screenWidth, screenHeight, ColorAlpha(BLACK, 0.8f));

            DrawText(winnerText, screenWidth / 2 - MeasureText(winnerText, 54) / 2, screenHeight / 2 - 80, 54, COLOR_ACCENT);

            const char* restartTxt = "Press [ SPACE ] or [ R ] to Play Again";
            DrawText(restartTxt, screenWidth / 2 - MeasureText(restartTxt, 22) / 2, screenHeight / 2 + 10, 22, WHITE);

            const char* menuTxt = "Press [ M ] for Main Menu";
            DrawText(menuTxt, screenWidth / 2 - MeasureText(menuTxt, 20) / 2, screenHeight / 2 + 50, 20, LIGHTGRAY);
        }
    }

    void Render() {
        BeginDrawing();
        DrawCourt();

        // Draw entities
        ball.Draw();
        p1.Draw();
        if (isTwoPlayer) {
            p2.Draw();
        } else {
            cpu.Draw();
        }

        // Draw HUD and State Overlays
        DrawHUD();

        EndDrawing();
    }

    void Cleanup() {
        audio.Cleanup();
    }
};

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================
int main() {
    const int screenWidth  = 1280;
    const int screenHeight = 800;

    // Window configuration
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Pong Game - 3 Modular Architecture (Raylib)");
    SetTargetFPS(60);

    GameManager game;
    game.Init(screenWidth, screenHeight);

    // Main Game Loop
    while (!WindowShouldClose()) {
        game.HandleInput();
        game.UpdatePhysics();
        game.Render();
    }

    game.Cleanup();
    CloseWindow();
    return 0;
}
