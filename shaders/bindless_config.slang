[[vk::binding(0, 0)]]
__DynamicResource<__DynamicResourceKind.Sampler> gSamplers[];

[[vk::binding(1, 0)]]
__DynamicResource<__DynamicResourceKind.General> gSampledImages[];

[[vk::binding(2, 0)]]
__DynamicResource<__DynamicResourceKind.General> gStorageImages[];

export T getDescriptorFromHandle<T>(DescriptorHandle<T> h) where T : IOpaqueDescriptor
{
    uint idx = ((uint2)h).x;

    if (T.kind == DescriptorKind.Sampler)
    {
        return gSamplers[idx].asOpaqueDescriptor<T>();
    }
    else if (T.kind == DescriptorKind.Texture)
    {
        if (T.descriptorAccess == DescriptorAccess::ReadWrite)
        {
            return gStorageImages[idx].asOpaqueDescriptor<T>();
        }
        else
        {
            return gSampledImages[idx].asOpaqueDescriptor<T>();
        }
    }

    return gSampledImages[idx].asOpaqueDescriptor<T>();
}

#define BINDLESS(idx, T) ((DescriptorHandle<T>) uint2(idx,0))

