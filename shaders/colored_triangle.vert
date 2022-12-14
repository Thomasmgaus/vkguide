#version 450

layout (location = 0) out vec3 outColor;

void main() {
    // array of positions
    const vec3 positions[3] = vec3[3](
        vec3(1.f,1.f,0.0f),
        vec3(-1.f,1.f,0.0f),
        vec3(0.f,-1.f,0.0f)
    );

    //colors for the triangle
    const vec3 colors[3] = vec3[3](
    // vec3 (R,G,B)
        vec3(1.0f, 0.0f,0.0f), //Red
        vec3(0.0f,1.0f,0.0f),  //Green
        vec3(00.f,0.0f,1.0f)   //Blue
    );
    gl_Position = vec4(positions[gl_VertexIndex], 1.0f);
    outColor = colors[gl_VertexIndex];
}
