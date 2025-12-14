# Cyseal

A toy rendering engine to experiment with DX12 and Vulkan.

# Environment

* Graphics API: DirectX 12
  * Vulkan is work in progress, but does not run at all.
* Language: C++20
* OS: Windows 11
* IDE: Visual Studio 2022

Run `Setup.ps1`, build `CysealSolution.sln`, and launch the test project.

# Screenshots

This project is at a very early stage. Only basic rendering features work with DX12 backend.

**Visibility Buffer**

<img src="https://github.com/user-attachments/assets/ec98caa1-b251-4e69-a8c3-5aced73af387" width="49%" />
<img src="https://github.com/user-attachments/assets/f662ea09-4868-49ed-9d14-a91cc5e1c762" width="49%" />
<img src="https://github.com/user-attachments/assets/e2f607ba-2459-4279-b769-658143fc43f0" width="49%" />
<img src="https://github.com/user-attachments/assets/72b8eaf6-f725-45b9-aa05-2b4d67161bbf" width="49%" />

Write visibility buffer (objectID + primID) in depth prepass. Calculate barycentric UV of triangles from visibility buffer. Generate gbuffers from barycentric UV.

**Hardware Raytracing**

<img src="https://user-images.githubusercontent.com/11644393/196040224-dafb600b-1be4-46e3-aa16-5335859c9e76.jpg" width="33%" /><img src="https://user-images.githubusercontent.com/11644393/201951222-44803f65-1d79-4691-bbe2-04a782dc515c.jpg" width="33%" /><img src="https://user-images.githubusercontent.com/11644393/202848012-1d8ebcf2-53fc-4f08-b61b-4199d5fefa55.jpg" width="33%" />
<img src="https://user-images.githubusercontent.com/11644393/211977004-d3ec684f-cc0c-4958-b378-a961caedfd8c.jpg" width="33%" /><img src="https://user-images.githubusercontent.com/11644393/234315027-8311aee9-5662-43f2-a47e-02080d452031.jpg" width="33%" /><img src="https://user-images.githubusercontent.com/11644393/234315105-bdb18772-7ab5-419e-9a52-4c7f07cfafe7.jpg" width="33%" /><img src="https://github.com/user-attachments/assets/867901d4-5547-4020-9f93-7f233a99acbb" width="33%" />

Use gbuffers to generate rays starting from surfaces. Accumulate lighting at hit points to generate indirect diffuse/specular reflection textures. Run realtime denoiser.

Generate rays starting from camera for full path tracing. Run offline denoiser.

# Features

* GPU-driven rendering
  * Bindless textures
  * GPU scene management
  * GPU culling and indirect draw
  * Visibility buffer
* Hardware Raytracing
  * Ray Traced Shadows
  * Raytraced indirect diffuse reflection
  * Raytraced indirect specular reflection and refraction with AMD FidelityFX Reflection Denoiser
  * Monte Carlo path tracing with Intel OpenImageDenoise
