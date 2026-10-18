// dxbc2glsl -- DXBC пары вершинный+пиксельный шейдер -> GLSL ES 3.0 в стиле Unity.
//
// Unity сама получает GLES-шейдеры так же: HLSL -> FXC (DXBC) -> HLSLcc -> GLSL.
// В бандлах ULTRAKILL лежит готовый DXBC, поэтому шаг HLSLcc можно повторить у
// себя и получить GLES3 для каждого варианта шейдера без исходников.
//
//   dxbc2glsl [-f флаги_hex] [-l es300|es310] vs.dxbc ps.dxbc out.glsl out.json
//
// out.glsl -- один текст «#ifdef VERTEX ... #endif #ifdef FRAGMENT ... #endif»,
// как его хранит Unity. out.json -- отражение (атрибуты, константы, буферы,
// текстуры) для заполнения m_Parameters подпрограммы.

#include "hlslcc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static bool read_file(const char* path, std::vector<char>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

static std::string esc(const std::string& s)
{
    std::string r;
    for (char c : s)
    {
        if (c == '"' || c == '\\') { r += '\\'; r += c; }
        else if (c == '\n') r += "\\n";
        else if ((unsigned char)c < 0x20) r += ' ';
        else r += c;
    }
    return r;
}

// Собирает всё, что HLSLcc сообщает об интерфейсе шейдера.
struct Reflect : public HLSLccReflection
{
    std::ostringstream items;
    std::string stage;
    bool first = true;
    std::string errors;

    void sep() { if (!first) items << ",\n"; first = false; }

    void OnDiagnostics(const std::string& error, int line, bool isError) override
    {
        errors += (isError ? "error: " : "warning: ") + error + "\n";
    }
    void OnInputBinding(const std::string& name, int bindIndex) override
    {
        sep();
        items << "  {\"stage\":\"" << stage << "\",\"kind\":\"input\",\"name\":\"" << esc(name)
              << "\",\"index\":" << bindIndex << "}";
    }
    bool OnConstantBuffer(const std::string& name, size_t bufferSize, size_t memberCount) override
    {
        sep();
        items << "  {\"stage\":\"" << stage << "\",\"kind\":\"cbuffer\",\"name\":\"" << esc(name)
              << "\",\"size\":" << bufferSize << ",\"members\":" << memberCount << "}";
        return true;
    }
    bool OnConstant(const std::string& name, int bindIndex, SHADER_VARIABLE_TYPE cType, int rows, int cols,
                    bool isMatrix, int arraySize, bool isUsed) override
    {
        sep();
        items << "  {\"stage\":\"" << stage << "\",\"kind\":\"constant\",\"name\":\"" << esc(name)
              << "\",\"offset\":" << bindIndex << ",\"type\":" << (int)cType << ",\"rows\":" << rows
              << ",\"cols\":" << cols << ",\"matrix\":" << (isMatrix ? "true" : "false")
              << ",\"array\":" << arraySize << ",\"used\":" << (isUsed ? "true" : "false") << "}";
        return true;
    }
    void OnConstantBufferBinding(const std::string& name, int bindIndex) override
    {
        sep();
        items << "  {\"stage\":\"" << stage << "\",\"kind\":\"cbuffer_binding\",\"name\":\"" << esc(name)
              << "\",\"index\":" << bindIndex << "}";
    }
    void OnTextureBinding(const std::string& name, int bindIndex, int samplerIndex, bool multisampled,
                          HLSLCC_TEX_DIMENSION dim, bool isUAV) override
    {
        sep();
        items << "  {\"stage\":\"" << stage << "\",\"kind\":\"texture\",\"name\":\"" << esc(name)
              << "\",\"index\":" << bindIndex << ",\"sampler\":" << samplerIndex << ",\"dim\":" << (int)dim
              << ",\"uav\":" << (isUAV ? "true" : "false") << "}";
    }
    void OnBufferBinding(const std::string& name, int bindIndex, bool isUAV) override
    {
        sep();
        items << "  {\"stage\":\"" << stage << "\",\"kind\":\"buffer\",\"name\":\"" << esc(name)
              << "\",\"index\":" << bindIndex << ",\"uav\":" << (isUAV ? "true" : "false") << "}";
    }
    void OnVertexProgramOutput(const std::string& name, const std::string& semantic, int semanticIndex) override
    {
        sep();
        items << "  {\"stage\":\"" << stage << "\",\"kind\":\"vs_output\",\"name\":\"" << esc(name)
              << "\",\"semantic\":\"" << esc(semantic) << "\",\"index\":" << semanticIndex << "}";
    }
    void OnFragmentOutputDeclaration(int numComponents, int outputIndex) override
    {
        sep();
        items << "  {\"stage\":\"" << stage << "\",\"kind\":\"fs_output\",\"components\":" << numComponents
              << ",\"index\":" << outputIndex << "}";
    }
};

int main(int argc, char** argv)
{
    // Флаги по умолчанию -- как у GLES3 в Unity: имена входов по семантике
    // (in_POSITION0), матрицы разложены (hlslcc_mtx4x4), глобалы вне UBO.
    unsigned int flags = HLSLCC_FLAG_UNIFORM_BUFFER_OBJECT | HLSLCC_FLAG_GLOBAL_CONSTS_NEVER_IN_UBO |
                         HLSLCC_FLAG_INOUT_APPEND_SEMANTIC_NAMES | HLSLCC_FLAG_COMBINE_TEXTURE_SAMPLERS |
                         HLSLCC_FLAG_DISABLE_GLOBALS_STRUCT | HLSLCC_FLAG_WRAP_UBO |
                         HLSLCC_FLAG_TRANSLATE_MATRICES | HLSLCC_FLAG_REMOVE_UNUSED_GLOBALS |
                         HLSLCC_FLAG_MOBILE_TARGET;
    GLLang lang = LANG_ES_300;

    int a = 1;
    for (; a < argc && argv[a][0] == '-'; a += 2)
    {
        if (a + 1 >= argc) break;
        if (!strcmp(argv[a], "-f")) flags = (unsigned)strtoul(argv[a + 1], nullptr, 16);
        else if (!strcmp(argv[a], "-l")) lang = !strcmp(argv[a + 1], "es310") ? LANG_ES_310 : LANG_ES_300;
    }
    if (argc - a < 4)
    {
        fprintf(stderr, "usage: dxbc2glsl [-f hexflags] [-l es300|es310] vs.dxbc ps.dxbc out.glsl out.json\n");
        return 2;
    }

    std::vector<char> vs, ps;
    if (!read_file(argv[a], vs) || !read_file(argv[a + 1], ps))
    {
        fprintf(stderr, "cannot read input\n");
        return 2;
    }

    GlExtensions ext = {};
    ext.ARB_explicit_attrib_location = 1;
    GLSLCrossDependencyData deps;
    HLSLccSamplerPrecisionInfo prec;
    Reflect refl;

    GLSLShader vsOut, psOut;
    refl.stage = "vertex";
    int okv = TranslateHLSLFromMem(vs.data(), flags, lang, &ext, &deps, prec, refl, &vsOut);
    refl.stage = "fragment";
    int okp = TranslateHLSLFromMem(ps.data(), flags, lang, &ext, &deps, prec, refl, &psOut);
    if (!okv || !okp)
    {
        fprintf(stderr, "translate failed (vs=%d ps=%d)\n%s", okv, okp, refl.errors.c_str());
        return 1;
    }

    std::ofstream g(argv[a + 2], std::ios::binary);
    g << "#ifdef VERTEX\n" << vsOut.sourceCode << "\n#endif\n#ifdef FRAGMENT\n" << psOut.sourceCode << "\n#endif\n";

    std::ofstream j(argv[a + 3], std::ios::binary);
    j << "{\"flags\":" << flags << ",\"items\":[\n" << refl.items.str() << "\n],\"textureSamplers\":[";
    for (size_t i = 0; i < psOut.textureSamplers.size(); i++)
        j << (i ? "," : "") << "\"" << esc(psOut.textureSamplers[i]) << "\"";
    j << "],\"diagnostics\":\"" << esc(refl.errors) << "\"}\n";
    return 0;
}
