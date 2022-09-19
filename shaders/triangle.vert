#version 450

// tells the fragment shader a variable of type vec3 will be at location 0
layout (location = 0) out vec3 outColor;

void main() {
    // three positions of x, y, z
    const vec3 positions[3] = vec3[3](
        vec3(1.0f,1.0f,0.0f),
        vec3(-1.0f,1.f,0.0f),
        vec3(0.0f,-1.f,0.0f)
    );
    //gl_Position = Tell the gpu what the position of the vertex is (gl_Position expects a vec4)
    //gl_VertexIndex = What number is the vertex being executed for
    gl_Position = vec4(positions[gl_VertexIndex], 1.0f);
}
