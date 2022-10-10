#include <sl12/gui.h>
#include <sl12/device.h>
#include <sl12/command_list.h>
#include <sl12/texture.h>
#include <sl12/texture_view.h>
#include <sl12/buffer.h>
#include <sl12/buffer_view.h>
#include <sl12/sampler.h>
#include <sl12/shader.h>
#include <sl12/descriptor.h>
#include <sl12/descriptor_heap.h>
#include <sl12/swapchain.h>
#include <sl12/root_signature.h>
#include <sl12/pipeline_state.h>
#include <sl12/descriptor_set.h>
#include <sl12/VSGui.h>
#include <sl12/PSGui.h>


namespace sl12
{
	namespace
	{
		static const u32	kMaxFrameCount = Swapchain::kMaxBuffer;

		struct VertexUniform
		{
			float	scale_[2];
			float	translate_[2];
		};	// struct VertexUniform

		struct ImDrawVertex
		{
			float	pos_[2];
			float	uv_[2];
			u32		color_;
		};	// struct ImDrawVertex

		static ImGuiKey GetImGuiKey(u64 wParam)
		{
		    switch (wParam)
		    {
		        case VK_TAB: return ImGuiKey_Tab;
		        case VK_LEFT: return ImGuiKey_LeftArrow;
		        case VK_RIGHT: return ImGuiKey_RightArrow;
		        case VK_UP: return ImGuiKey_UpArrow;
		        case VK_DOWN: return ImGuiKey_DownArrow;
		        case VK_PRIOR: return ImGuiKey_PageUp;
		        case VK_NEXT: return ImGuiKey_PageDown;
		        case VK_HOME: return ImGuiKey_Home;
		        case VK_END: return ImGuiKey_End;
		        case VK_INSERT: return ImGuiKey_Insert;
		        case VK_DELETE: return ImGuiKey_Delete;
		        case VK_BACK: return ImGuiKey_Backspace;
		        case VK_SPACE: return ImGuiKey_Space;
		        case VK_RETURN: return ImGuiKey_Enter;
		        case VK_ESCAPE: return ImGuiKey_Escape;
		        case VK_OEM_7: return ImGuiKey_Apostrophe;
		        case VK_OEM_COMMA: return ImGuiKey_Comma;
		        case VK_OEM_MINUS: return ImGuiKey_Minus;
		        case VK_OEM_PERIOD: return ImGuiKey_Period;
		        case VK_OEM_2: return ImGuiKey_Slash;
		        case VK_OEM_1: return ImGuiKey_Semicolon;
		        case VK_OEM_PLUS: return ImGuiKey_Equal;
		        case VK_OEM_4: return ImGuiKey_LeftBracket;
		        case VK_OEM_5: return ImGuiKey_Backslash;
		        case VK_OEM_6: return ImGuiKey_RightBracket;
		        case VK_OEM_3: return ImGuiKey_GraveAccent;
		        case VK_CAPITAL: return ImGuiKey_CapsLock;
		        case VK_SCROLL: return ImGuiKey_ScrollLock;
		        case VK_NUMLOCK: return ImGuiKey_NumLock;
		        case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
		        case VK_PAUSE: return ImGuiKey_Pause;
		        case VK_NUMPAD0: return ImGuiKey_Keypad0;
		        case VK_NUMPAD1: return ImGuiKey_Keypad1;
		        case VK_NUMPAD2: return ImGuiKey_Keypad2;
		        case VK_NUMPAD3: return ImGuiKey_Keypad3;
		        case VK_NUMPAD4: return ImGuiKey_Keypad4;
		        case VK_NUMPAD5: return ImGuiKey_Keypad5;
		        case VK_NUMPAD6: return ImGuiKey_Keypad6;
		        case VK_NUMPAD7: return ImGuiKey_Keypad7;
		        case VK_NUMPAD8: return ImGuiKey_Keypad8;
		        case VK_NUMPAD9: return ImGuiKey_Keypad9;
		        case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
		        case VK_DIVIDE: return ImGuiKey_KeypadDivide;
		        case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
		        case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
		        case VK_ADD: return ImGuiKey_KeypadAdd;
		        case VK_LSHIFT: return ImGuiKey_LeftShift;
		        case VK_LCONTROL: return ImGuiKey_LeftCtrl;
		        case VK_LMENU: return ImGuiKey_LeftAlt;
		        case VK_LWIN: return ImGuiKey_LeftSuper;
		        case VK_RSHIFT: return ImGuiKey_RightShift;
		        case VK_RCONTROL: return ImGuiKey_RightCtrl;
		        case VK_RMENU: return ImGuiKey_RightAlt;
		        case VK_RWIN: return ImGuiKey_RightSuper;
		        case VK_APPS: return ImGuiKey_Menu;
		        case '0': return ImGuiKey_0;
		        case '1': return ImGuiKey_1;
		        case '2': return ImGuiKey_2;
		        case '3': return ImGuiKey_3;
		        case '4': return ImGuiKey_4;
		        case '5': return ImGuiKey_5;
		        case '6': return ImGuiKey_6;
		        case '7': return ImGuiKey_7;
		        case '8': return ImGuiKey_8;
		        case '9': return ImGuiKey_9;
		        case 'A': return ImGuiKey_A;
		        case 'B': return ImGuiKey_B;
		        case 'C': return ImGuiKey_C;
		        case 'D': return ImGuiKey_D;
		        case 'E': return ImGuiKey_E;
		        case 'F': return ImGuiKey_F;
		        case 'G': return ImGuiKey_G;
		        case 'H': return ImGuiKey_H;
		        case 'I': return ImGuiKey_I;
		        case 'J': return ImGuiKey_J;
		        case 'K': return ImGuiKey_K;
		        case 'L': return ImGuiKey_L;
		        case 'M': return ImGuiKey_M;
		        case 'N': return ImGuiKey_N;
		        case 'O': return ImGuiKey_O;
		        case 'P': return ImGuiKey_P;
		        case 'Q': return ImGuiKey_Q;
		        case 'R': return ImGuiKey_R;
		        case 'S': return ImGuiKey_S;
		        case 'T': return ImGuiKey_T;
		        case 'U': return ImGuiKey_U;
		        case 'V': return ImGuiKey_V;
		        case 'W': return ImGuiKey_W;
		        case 'X': return ImGuiKey_X;
		        case 'Y': return ImGuiKey_Y;
		        case 'Z': return ImGuiKey_Z;
		        case VK_F1: return ImGuiKey_F1;
		        case VK_F2: return ImGuiKey_F2;
		        case VK_F3: return ImGuiKey_F3;
		        case VK_F4: return ImGuiKey_F4;
		        case VK_F5: return ImGuiKey_F5;
		        case VK_F6: return ImGuiKey_F6;
		        case VK_F7: return ImGuiKey_F7;
		        case VK_F8: return ImGuiKey_F8;
		        case VK_F9: return ImGuiKey_F9;
		        case VK_F10: return ImGuiKey_F10;
		        case VK_F11: return ImGuiKey_F11;
		        case VK_F12: return ImGuiKey_F12;
		        default: return ImGuiKey_None;
		    }
		}
	}	// namespace


	Gui* Gui::guiHandle_ = nullptr;

	//----
	bool Gui::Initialize(Device* pDevice, DXGI_FORMAT rtFormat, DXGI_FORMAT dsFormat)
	{
		Destroy();

		if (guiHandle_)
		{
			return false;
		}

		pOwner_ = pDevice;
		guiHandle_ = this;

		// create default context.
		ImGui::CreateContext();

		// create shaders.
		pVShader_ = new Shader();
		pPShader_ = new Shader();
		if (!pVShader_ || !pPShader_)
		{
			return false;
		}
		if (!pVShader_->Initialize(pDevice, ShaderType::Vertex, kVSGui, sizeof(kVSGui)))
		{
			return false;
		}
		if (!pPShader_->Initialize(pDevice, ShaderType::Pixel, kPSGui, sizeof(kPSGui)))
		{
			return false;
		}

		// create sampler.
		{
			pFontSampler_ = new Sampler();
			if (!pFontSampler_)
			{
				return false;
			}
			D3D12_SAMPLER_DESC desc{};
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			if (!pFontSampler_->Initialize(pDevice, desc))
			{
				return false;
			}
		}

		// create root signature.
		{
			pRootSig_ = new RootSignature();
			if (!pRootSig_->Initialize(pDevice, pVShader_, pPShader_, nullptr, nullptr, nullptr))
			{
				return false;
			}

			pDescSet_ = new DescriptorSet();
		}

		// create PSO.
		{
			D3D12_INPUT_ELEMENT_DESC elementDescs[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 0,                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, sizeof(float) * 2, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, sizeof(float) * 4, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			};

			GraphicsPipelineStateDesc desc{};

			desc.blend.sampleMask = UINT_MAX;
			desc.blend.rtDesc[0].isBlendEnable = true;
			desc.blend.rtDesc[0].srcBlendColor = D3D12_BLEND_SRC_ALPHA;
			desc.blend.rtDesc[0].dstBlendColor = D3D12_BLEND_INV_SRC_ALPHA;
			desc.blend.rtDesc[0].blendOpColor = D3D12_BLEND_OP_ADD;
			desc.blend.rtDesc[0].srcBlendAlpha = D3D12_BLEND_ONE;
			desc.blend.rtDesc[0].dstBlendAlpha = D3D12_BLEND_ZERO;
			desc.blend.rtDesc[0].blendOpAlpha = D3D12_BLEND_OP_ADD;
			desc.blend.rtDesc[0].writeMask = D3D12_COLOR_WRITE_ENABLE_ALL;

			desc.depthStencil.isDepthEnable = (dsFormat != DXGI_FORMAT_UNKNOWN);
			desc.depthStencil.isDepthWriteEnable = (dsFormat != DXGI_FORMAT_UNKNOWN);
			desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

			desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
			desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
			desc.rasterizer.isFrontCCW = false;
			desc.rasterizer.isDepthClipEnable = true;
			desc.multisampleCount = 1;

			desc.inputLayout.numElements = ARRAYSIZE(elementDescs);
			desc.inputLayout.pElements = elementDescs;

			desc.pRootSignature = pRootSig_;
			desc.pVS = pVShader_;
			desc.pPS = pPShader_;
			desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			desc.numRTVs = 0;
			desc.rtvFormats[desc.numRTVs++] = rtFormat;
			desc.dsvFormat = dsFormat;

			pPipelineState_ = new GraphicsPipelineState();
			if (!pPipelineState_->Initialize(pDevice, desc))
			{
				return false;
			}
		}

		// create constant buffer.
		pConstantBuffers_ = new Buffer[kMaxFrameCount];
		pConstantBufferViews_ = new ConstantBufferView[kMaxFrameCount];
		if (!pConstantBuffers_ || !pConstantBufferViews_)
		{
			return false;
		}
		BufferDesc creationDesc{};
		creationDesc.size = sizeof(VertexUniform);
		creationDesc.usage = ResourceUsage::ConstantBuffer;
		creationDesc.heap = BufferHeap::Dynamic;
		for (u32 i = 0; i < kMaxFrameCount; i++)
		{
			if (!pConstantBuffers_[i].Initialize(pDevice, creationDesc))
			{
				return false;
			}
			if (!pConstantBufferViews_[i].Initialize(pDevice, &pConstantBuffers_[i]))
			{
				return false;
			}
		}

		// create vertex/index buffer.
		pVertexBuffers_ = new Buffer[kMaxFrameCount];
		pVertexBufferViews_ = new VertexBufferView[kMaxFrameCount];
		pIndexBuffers_ = new Buffer[kMaxFrameCount];
		pIndexBufferViews_ = new IndexBufferView[kMaxFrameCount];
		if (!pVertexBuffers_ || !pVertexBufferViews_ || !pIndexBuffers_ || !pIndexBufferViews_)
		{
			return false;
		}

		return true;
	}

	//----
	void Gui::Destroy()
	{
		if (pOwner_)
		{
			sl12::SafeDelete(pVShader_);
			sl12::SafeDelete(pPShader_);
			sl12::SafeDelete(pFontTextureView_);
			sl12::SafeDelete(pFontTexture_);
			sl12::SafeDelete(pFontSampler_);

			sl12::SafeDeleteArray(pConstantBufferViews_);
			sl12::SafeDeleteArray(pConstantBuffers_);
			sl12::SafeDeleteArray(pVertexBufferViews_);
			sl12::SafeDeleteArray(pVertexBuffers_);
			sl12::SafeDeleteArray(pIndexBufferViews_);
			sl12::SafeDeleteArray(pIndexBuffers_);

			sl12::SafeDelete(pRootSig_);
			sl12::SafeDelete(pPipelineState_);
			sl12::SafeDelete(pDescSet_);

			ImGui::DestroyContext();

			pOwner_ = nullptr;
		}
		guiHandle_ = nullptr;
	}

	//----
	bool Gui::CreateFontImage(Device* pDevice, CommandList* pCmdList)
	{
		if (!pOwner_)
		{
			return false;
		}

		ImGuiIO& io = ImGui::GetIO();

		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		size_t upload_size = width * height * 4 * sizeof(char);

		// create texture.
		TextureDesc desc;
		desc.dimension = TextureDimension::Texture2D;
		desc.width = static_cast<u32>(width);
		desc.height = static_cast<u32>(height);
		desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		
		pFontTexture_ = new Texture();
		if (!pFontTexture_)
		{
			return false;
		}

		if (!pFontTexture_->InitializeFromImageBin(pDevice, pCmdList, desc, pixels))
		{
			return false;
		}

		// create SRV.
		pFontTextureView_ = new TextureView();
		if (!pFontTextureView_)
		{
			return false;
		}
		if (!pFontTextureView_->Initialize(pDevice, pFontTexture_))
		{
			return false;
		}

		// set texture ID.
		io.Fonts->SetTexID(pFontTexture_);

		return true;
	}

	//----
	void Gui::LoadDrawCommands(CommandList* pCmdList)
	{
		pDrawCommandList_ = pCmdList;
		Gui::RenderDrawList(ImGui::GetDrawData());
	}

	//----
	void Gui::RenderDrawList(ImDrawData* draw_data)
	{
		ImGuiIO& io = ImGui::GetIO();

		Gui* pThis = guiHandle_;
		Device* pDevice = pThis->pOwner_;
		CommandList* pCmdList = pThis->pDrawCommandList_;
		u32 frameIndex = pThis->frameIndex_;

		// 頂点バッファ生成
		Buffer& vbuffer = pThis->pVertexBuffers_[frameIndex];
		VertexBufferView& vbView = pThis->pVertexBufferViews_[frameIndex];
		size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
		if (vbuffer.GetBufferDesc().size < vertex_size)
		{
			BufferDesc creationDesc{};
			creationDesc.size = vertex_size;
			creationDesc.stride = sizeof(ImDrawVert);
			creationDesc.usage = ResourceUsage::VertexBuffer;
			creationDesc.heap = BufferHeap::Dynamic;

			vbuffer.Destroy();
			vbuffer.Initialize(pDevice, creationDesc);

			vbView.Destroy();
			vbView.Initialize(pDevice, &vbuffer);
		}

		// インデックスバッファ生成
		Buffer& ibuffer = pThis->pIndexBuffers_[frameIndex];
		IndexBufferView& ibView = pThis->pIndexBufferViews_[frameIndex];
		size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
		if (ibuffer.GetBufferDesc().size < index_size)
		{
			BufferDesc creationDesc{};
			creationDesc.size = index_size;
			creationDesc.stride = sizeof(ImDrawIdx);
			creationDesc.usage = ResourceUsage::IndexBuffer;
			creationDesc.heap = BufferHeap::Dynamic;

			ibuffer.Destroy();
			ibuffer.Initialize(pDevice, creationDesc);

			ibView.Destroy();
			ibView.Initialize(pDevice, &ibuffer);
		}

		// 頂点・インデックスのメモリを上書き
		{
			ImDrawVert* vtx_dst = static_cast<ImDrawVert*>(vbuffer.Map());
			ImDrawIdx* idx_dst = static_cast<ImDrawIdx*>(ibuffer.Map());

			for (int n = 0; n < draw_data->CmdListsCount; n++)
			{
				const ImDrawList* cmd_list = draw_data->CmdLists[n];
				memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
				memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
				vtx_dst += cmd_list->VtxBuffer.Size;
				idx_dst += cmd_list->IdxBuffer.Size;
			}

			vbuffer.Unmap();
			ibuffer.Unmap();
		}

		// 定数バッファ更新
		{
			Buffer& cb = pThis->pConstantBuffers_[frameIndex];
			float* p = static_cast<float*>(cb.Map());
			p[0] = 2.0f / io.DisplaySize.x;
			p[1] = -2.0f / io.DisplaySize.y;
			p[2] = -1.0f;
			p[3] = 1.0f;
			cb.Unmap();
		}

		// レンダリング開始
		// NOTE: レンダーターゲットは設定済みとする

		// パイプラインステート設定
		ID3D12GraphicsCommandList* pNativeCmdList = pCmdList->GetCommandList();
		pNativeCmdList->SetPipelineState(pThis->pPipelineState_->GetPSO());

		// DescriptorSet設定
		ConstantBufferView& cbView = pThis->pConstantBufferViews_[frameIndex];
		pThis->pDescSet_->Reset();
		pThis->pDescSet_->SetVsCbv(0, cbView.GetDescInfo().cpuHandle);
		pThis->pDescSet_->SetPsSrv(0, pThis->pFontTextureView_->GetDescInfo().cpuHandle);
		pThis->pDescSet_->SetPsSampler(0, pThis->pFontSampler_->GetDescInfo().cpuHandle);

		// RootSigとDescSetを設定
		pCmdList->SetGraphicsRootSignatureAndDescriptorSet(pThis->pRootSig_, pThis->pDescSet_);

		// DrawCall
		D3D12_VERTEX_BUFFER_VIEW views[] = { vbView.GetView() };
		pNativeCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pNativeCmdList->IASetVertexBuffers(0, _countof(views), views);
		pNativeCmdList->IASetIndexBuffer(&ibView.GetView());

		// DrawCall
		int vtx_offset = 0;
		int idx_offset = 0;
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
			{
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
				if (pcmd->UserCallback)
				{
					pcmd->UserCallback(cmd_list, pcmd);
				}
				else
				{
					D3D12_RECT rect;
					rect.left = static_cast<s32>(pcmd->ClipRect.x);
					rect.top = static_cast<s32>(pcmd->ClipRect.y);
					rect.right = static_cast<s32>(pcmd->ClipRect.z);
					rect.bottom = static_cast<s32>(pcmd->ClipRect.w);
					pNativeCmdList->RSSetScissorRects(1, &rect);

					pNativeCmdList->DrawIndexedInstanced(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
				}
				idx_offset += pcmd->ElemCount;
			}
			vtx_offset += cmd_list->VtxBuffer.Size;
		}
	}

	//----
	// 新しいフレームの開始
	void Gui::BeginNewFrame(CommandList* pDrawCmdList, uint32_t frameWidth, uint32_t frameHeight, const InputData& input, float frameScale, float timeStep)
	{
		ImGuiIO& io = ImGui::GetIO();

		// フレームバッファのサイズを指定する
		io.DisplaySize = ImVec2((float)frameWidth, (float)frameHeight);
		io.DisplayFramebufferScale = ImVec2(frameScale, frameScale);

		// 時間進行を指定
		io.DeltaTime = timeStep;

		// マウスによる操作
		io.MousePos = ImVec2((float)input.mouseX, (float)input.mouseY);
		io.MouseDown[0] = (input.mouseButton & MouseButton::Left) != 0;
		io.MouseDown[1] = (input.mouseButton & MouseButton::Right) != 0;
		io.MouseDown[2] = (input.mouseButton & MouseButton::Middle) != 0;

		// key event.
		if (input.key != 0)
		{
			auto ikey = GetImGuiKey(input.key);
			io.AddKeyEvent(ikey, input.keyDown);
			io.SetKeyEventNativeData(ikey, (int)input.key, input.scancode);
		}
		if (input.chara != 0)
		{
			io.AddInputCharacterUTF16(input.chara);
		}

		// 新規フレーム開始
		ImGui::NewFrame();

		frameIndex_ = (frameIndex_ + 1) % kMaxFrameCount;
		pDrawCommandList_ = pDrawCmdList;
	}

}	// namespace sl12


// eof
