module neuralResources;
import common;

// PathTracing owns this complete set-6 contract. It is deliberately not part
// of the global aggregation module, so non-RT shaders cannot acquire the ABI
// merely by importing `common`.
[[vk::binding(0, 6)]]
public StructuredBuffer<RtNeuralMaterialRef> gRtNeuralMaterialRefs;

[[vk::binding(1, 6)]]
public ByteAddressBuffer gNeuralModelParameters;

[[vk::binding(2, 6)]]
public Sampler2D<float4> gNeuralLatentTexture0;

[[vk::binding(3, 6)]]
public Sampler2D<float4> gNeuralLatentTexture1;
