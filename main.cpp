#include <iostream>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <geodesy/math.h>
#include <geodesy/gpu.h>

using namespace geodesy;

std::set<std::string> load_glfw_instance_extensions() {
	std::set<std::string> Result;

	uint32_t ExtensionCount = 0;
	const char** Extensions = glfwGetRequiredInstanceExtensions(&ExtensionCount);
	for (uint32_t ExtensionIndex = 0; ExtensionIndex < ExtensionCount; ++ExtensionIndex) {
		Result.insert(std::string(Extensions[ExtensionIndex]));
	}

	return Result;
}

int main(int aArgCount, char* aArgValues[]) {
	std::cout << "Hello, World!" << std::endl;

	glfwInit();

	{
		// Vulkan Instance
		std::shared_ptr<gpu::instance> Instance;

		// GPU Resources
		std::shared_ptr<gpu::device> PrimaryDevice;
		std::shared_ptr<gpu::context> Context;

		// Window Resources
		GLFWwindow *Window = NULL;
		VkSurfaceKHR Surface = VK_NULL_HANDLE;
		std::shared_ptr<gpu::swapchain> Swapchain;

		math::vec<unsigned int, 3> Resolution = { 800, 600, 1 };

		// Setup Rendering Contexts
		{
			std::set<std::string> InstanceLayers = { "VK_LAYER_KHRONOS_validation" };
			std::set<std::string> InstanceExtensions = load_glfw_instance_extensions();

			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

			// Create System Window.
			Window = glfwCreateWindow(Resolution[0], Resolution[1], "Vulkan Window", NULL, NULL);

			// Insure instances gets cleared before glfwTerminate is called.
			Instance = geodesy::make<gpu::instance>((void*)vkGetInstanceProcAddr, std::array<int, 3>{ 1, 2, 0 }, InstanceLayers, InstanceExtensions);

			// Create Vulkan Surface for window.
			glfwCreateWindowSurface(Instance->Handle, Window, NULL, &Surface);

			auto DeviceList = Instance->get_devices();
			for (auto& Device : DeviceList) {
				// Get first discrete GPU we find.
				if (Device->Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
					PrimaryDevice = Device;
					break;
				}
			}

			std::set<std::string> ContextLayers;
			std::set<std::string> ContextExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
			std::vector<unsigned int> ContextOperations = {
				gpu::device::operation::GRAPHICS | gpu::device::operation::COMPUTE,
				gpu::device::operation::TRANSFER
			};

			// GPU Device Context
			Context = Instance->create_context(PrimaryDevice, ContextOperations, ContextLayers, ContextExtensions);

			gpu::swapchain::create_info SwapchainCreateInfo;
			SwapchainCreateInfo.FrameCount					= 3;
			SwapchainCreateInfo.FrameRate					= 60.0f;
			SwapchainCreateInfo.PixelFormat					= gpu::image::format::B8G8R8A8_UNORM;
			SwapchainCreateInfo.ColorSpace					= gpu::swapchain::colorspace::SRGB_NONLINEAR;
			SwapchainCreateInfo.ImageUsage					= gpu::image::usage::COLOR_ATTACHMENT | gpu::image::usage::SAMPLED;
			SwapchainCreateInfo.CompositeAlpha				= gpu::swapchain::composite::ALPHA_OPAQUE;
			SwapchainCreateInfo.PresentMode					= gpu::swapchain::present_mode::FIFO;
			SwapchainCreateInfo.Clipped						= VK_TRUE;

			// Create Swapchain.
			Swapchain = Context->create<gpu::swapchain>(Surface, SwapchainCreateInfo);
		}

		std::shared_ptr<gpu::buffer> VertexBuffer;
		std::shared_ptr<gpu::pipeline> RasterizationPipeline;
		std::shared_ptr<gpu::command_pool> CommandPool;
		// This is why I hate swapchains.
		std::vector<std::shared_ptr<gpu::command_buffer>> ClearScreenCommandBuffer(Swapchain->Image.size());
		std::vector<std::shared_ptr<gpu::command_buffer>> DrawCall(Swapchain->Image.size());
		std::vector<std::shared_ptr<gpu::command_buffer>> FinalTransitionCommandBuffer(Swapchain->Image.size());

		// Do Stuff
		{
			// Vertex Definition
			struct vertex {
				float Position[3];
				float Color[4];
			};

			// Single Triangle Vertex Data
			vertex VertexData[] = {
				// X     Y     Z       R     G     B     A
				{ { 0.0f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
				{ { 0.5f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
				{ { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
			};

			// Vertex Shader.
			std::string VertexShaderSource = R"(
				#version 450
				#extension GL_ARB_separate_shader_objects : enable

				layout (location = 0) in vec3 VertexPosition;
				layout (location = 1) in vec4 VertexColor;

				layout (location = 0) out vec4 FragmentColor;

				void main() {
					gl_Position = vec4(VertexPosition, 1.0);
					FragmentColor = VertexColor;
				}
			)";

			// Fragment Shader.
			std::string FragmentShaderSource = R"(
				#version 450
				#extension GL_ARB_separate_shader_objects : enable

				layout (location = 0) in vec4 FragmentColor;

				layout (location = 0) out vec4 PixelColor;

				void main() {
					PixelColor = FragmentColor;
				}
			)";

			// Generate GPU resources
			gpu::buffer::create_info VertexBufferCreateInfo;
			VertexBufferCreateInfo.Memory 			= gpu::device::memory::DEVICE_LOCAL | gpu::device::memory::HOST_VISIBLE | gpu::device::memory::HOST_COHERENT;
			VertexBufferCreateInfo.Usage 			= gpu::buffer::usage::VERTEX;
			VertexBufferCreateInfo.ElementCount 	= sizeof(VertexData) / sizeof(VertexData[0]);

			VertexBuffer = Context->create<gpu::buffer>(
				VertexBufferCreateInfo,
				sizeof(VertexData),
				VertexData
			);

			// Compile Shader Sources into ASTs
			std::shared_ptr<gpu::shader> VertexShader = geodesy::make<gpu::shader>(gpu::shader::VERTEX, VertexShaderSource);
			std::shared_ptr<gpu::shader> FragmentShader = geodesy::make<gpu::shader>(gpu::shader::FRAGMENT, FragmentShaderSource);
			std::vector<std::shared_ptr<gpu::shader>> ShaderList = { VertexShader, FragmentShader };

			// Create Rasterizer SPIRV Binaries and Metadata Reflection
			std::shared_ptr<gpu::pipeline::rasterizer> Rasterizer = geodesy::make<gpu::pipeline::rasterizer>(ShaderList);

			// Describe the Vertex Buffer Layout to the Rasterizer
			Rasterizer->bind(0, sizeof(vertex), 0, offsetof(vertex, Position), gpu::pipeline::input_rate::VERTEX); // Position
			Rasterizer->bind(0, sizeof(vertex), 1, offsetof(vertex, Color), gpu::pipeline::input_rate::VERTEX); // Color

			// Describe the Swapchain Image to the Rasterizer
			Rasterizer->attach(0, Swapchain->Image[0]["Color"]);

			// We are rendering triangles, with simple polygon fill.
			Rasterizer->Resolution = { Resolution[0], Resolution[1], 1 };
			Rasterizer->PrimitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			Rasterizer->PolygonMode = VK_POLYGON_MODE_FILL;

			// Generate Actual GPU Rasterization Pipeline
			RasterizationPipeline = Context->create<gpu::pipeline>(Rasterizer);

			// Create Command Pool for Draw Calls
			CommandPool = Context->create<gpu::command_pool>(gpu::device::operation::GRAPHICS);

			// Create Draw Calls for swapchain.
			for (size_t i = 0; i < Swapchain->Image.size(); ++i) {
				// Transition swapchain image layout to SHADER READ
				{
					auto CommandBuffer = CommandPool->create<gpu::command_buffer>();
					CommandBuffer->begin();
					Swapchain->Image[i]["Color"]->transition(CommandBuffer.get(), gpu::image::layout::PRESENT_SRC_KHR, gpu::image::layout::SHADER_READ_ONLY_OPTIMAL);
					CommandBuffer->end();
					ClearScreenCommandBuffer[i] = CommandBuffer;
				}

				// Actual Draw Call
				{
					std::vector<std::shared_ptr<gpu::image>> imageVec = { Swapchain->Image[i]["Color"] };
					std::vector<std::shared_ptr<gpu::buffer>> bufferVec = { VertexBuffer };
					auto CommandBuffer = CommandPool->create<gpu::rasterization_call>(RasterizationPipeline, imageVec, bufferVec);
					DrawCall[i] = CommandBuffer;
				}

				// Transition back for presentation.
				{
					auto CommandBuffer = CommandPool->create<gpu::command_buffer>();
					CommandBuffer->begin();
					Swapchain->Image[i]["Color"]->transition(CommandBuffer.get(), gpu::image::layout::SHADER_READ_ONLY_OPTIMAL, gpu::image::layout::PRESENT_SRC_KHR);
					CommandBuffer->end();
					FinalTransitionCommandBuffer[i] = CommandBuffer;
				}
			}
		}

		// Main Loop
		while (!glfwWindowShouldClose(Window)) {
			glfwPollEvents();

			// Get index of next swapchain image.
			VkResult Result = Swapchain->next_frame();

			// Acquire next image from swapchain.
			std::pair<std::shared_ptr<gpu::semaphore>, std::shared_ptr<gpu::semaphore>> AcquirePresentSemaphores = Swapchain->get_acquire_present_semaphore_pair();

			std::shared_ptr<gpu::command_batch> Submission = geodesy::make<gpu::command_batch>();

			// Make sure image is acquired before drawing.
			Submission->WaitSemaphoreList.push_back(AcquirePresentSemaphores.first);
			Submission->WaitStageList.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

			// Add command buffers to submission batch.
			Submission->CommandBufferList.push_back(ClearScreenCommandBuffer[Swapchain->DrawIndex]);
			Submission->CommandBufferList.push_back(DrawCall[Swapchain->DrawIndex]);
			Submission->CommandBufferList.push_back(FinalTransitionCommandBuffer[Swapchain->DrawIndex]);

			// Make sure presentation waits on rendering.
			Submission->SignalSemaphoreList.push_back(AcquirePresentSemaphores.second);

			// Execute Draw Call for acquired image.
			Context->execute_and_wait(gpu::device::operation::GRAPHICS, std::vector<std::shared_ptr<gpu::command_batch>>{ Submission });

		}

	}

	glfwTerminate();

	return 0;
}