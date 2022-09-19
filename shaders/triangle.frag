#version 450

// this decleration means we will output for location 0 as vec4
layout (location = 0) out vec4 outFragColor;

void main() {
    outFragColor = vec4(1.f,0.f,0.f,1.0f);
}
