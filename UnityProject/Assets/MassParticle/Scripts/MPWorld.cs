using UnityEngine;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;


public unsafe class MPWorld : MonoBehaviour
{
    static List<MPWorld> _instances;
    static int _update_count;
    public static List<MPWorld> instances
    {
        get
        {
            if (_instances == null) { _instances = new List<MPWorld>(); }
            return _instances;
        }
    }

    public delegate void ParticleProcessor(MPWorld world, int numParticles, MPParticle* particles);
    public delegate void GatheredHitProcessor(MPWorld world, int numColliders, MPHitData* hits);

    public MPUpdateMode updateMode = MPUpdateMode.Immediate;
    public bool enableInteractions = true;
    public bool enableColliders = true;
    public bool enableForces = true;
    public MPSolverType solverType = MPSolverType.Impulse;
    public float force = 1.0f;
    public float particleLifeTime;
    public float timeScale = 0.6f;
    public float deceleration;
    public float pressureStiffness;
    public float wallStiffness;
    public Vector3 coordScale;
    public bool include3DColliders = true;
    public bool include2DColliders = true;
    public int divX = 256;
    public int divY = 1;
    public int divZ = 256;
    public float particleSize = 0.08f;
    public int maxParticleNum = 65536;
    public int particleNum = 0;
    public Material mat;

    public ParticleProcessor particleProcessor;
    public GatheredHitProcessor gatheredHitProcessor;
    public List<GameObject> colliders;
    IntPtr context;
    MPRenderer mprenderer;

    public const int dataTextureWidth = 3072;
    public const int dataTextureHeight = 2048;
    RenderTexture dataTexture;
    bool dataTextureNeedsUpdate;
    ComputeBuffer dataBuffer;
    bool dataBufferNeedsUpdate;

    public IntPtr GetContext() { return context; }


    public RenderTexture GetDataTexture()
    {
        if (dataTexture == null)
        {
            dataTexture = new RenderTexture(dataTextureWidth, dataTextureHeight, 0, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Default);
            dataTexture.isPowerOfTwo = false;
            dataTexture.filterMode = FilterMode.Point;
            dataTexture.Create();
        }
        if (dataTextureNeedsUpdate)
        {
            dataTextureNeedsUpdate = false;
            particleNum = MPAPI.mpUpdateDataTexture(context, dataTexture.GetNativeTexturePtr());
        }
        return dataTexture;
    }

    public ComputeBuffer GetDataBuffer()
    {
        if(dataBuffer == null) {
            dataBuffer = new ComputeBuffer(maxParticleNum, 48);
        }
        if (dataBufferNeedsUpdate)
        {
            dataBufferNeedsUpdate = false;
            particleNum = MPAPI.mpUpdateDataBuffer(context, dataBuffer);
        }
        return dataBuffer;
    }


    MPWorld()
    {
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
        MPAPI.mphInitialize();
#endif
        particleProcessor = DefaultParticleProcessor;
        gatheredHitProcessor = DefaultGatheredHitProcessor;
    }

    void OnEnable()
    {
        instances.Add(this);
        context = MPAPI.mpCreateContext();
    }

    void OnDisable()
    {
        colliders.Clear();
        MPAPI.mpDestroyContext(context);
        if (dataTexture != null) { DestroyImmediate(dataTexture); }
        if (dataBuffer != null) { dataBuffer.Release(); }
        instances.Remove(this);
    }

    void Start()
    {
        mprenderer = GetComponent<MPRenderer>();
    }

    void Update()
    {
        if (Time.deltaTime != 0.0f)
        {
            if (_update_count++ == 0)
            {
                ActualUpdate();
            }
        }
    }

    void LateUpdate()
    {
        --_update_count;
    }


    static void ActualUpdate()
    {
        foreach (MPWorld w in instances)
        {
            if (w.updateMode == MPUpdateMode.Immediate)
            {
                w.ImmediateUpdate();
            }
        }

        // deferred update
        foreach (MPWorld w in instances)
        {
            if (w.updateMode == MPUpdateMode.Deferred)
            {
                MPAPI.mpEndUpdate(w.GetContext());
            }
        }
        foreach (MPWorld w in instances)
        {
            if (w.updateMode == MPUpdateMode.Deferred)
            {
                w.ExecuteProcessors();

                MPAPI.mpClearCollidersAndForces(w.GetContext());
                w.UpdateKernelParams();
                w.UpdateMPObjects();
            }
        }
        foreach (MPWorld w in instances)
        {
            if (w.updateMode == MPUpdateMode.Deferred)
            {
                MPAPI.mpBeginUpdate(w.GetContext(), Time.deltaTime);
            }
        }


        foreach (MPWorld w in instances)
        {
            w.dataTextureNeedsUpdate = true;
            w.dataBufferNeedsUpdate = true;
        }
    }

    void ImmediateUpdate()
    {
        UpdateKernelParams();
        UpdateMPObjects();
        MPAPI.mpUpdate(context, Time.deltaTime);
        ExecuteProcessors();
        MPAPI.mpClearCollidersAndForces(context);
    }

    //void DeferredUpdate()
    //{
    //    MPAPI.mpEndUpdate(context);
    //    ExecuteProcessors();

    //    MPAPI.mpClearCollidersAndForces(context);
    //    UpdateKernelParams();
    //    UpdateMPObjects();
    //    MPAPI.mpBeginUpdate(context, Time.deltaTime);
    //}


    void OnDrawGizmos()
    {
        Gizmos.color = Color.yellow;
        Gizmos.DrawWireCube(transform.position, transform.localScale*2.0f);
    }


    void UpdateKernelParams()
    {
        MPKernelParams p = MPAPI.mpGetKernelParams(context);
        p.WorldCenter = transform.position;
        p.WorldSize = transform.localScale;
        p.WorldDiv_x = divX;
        p.WorldDiv_y = divY;
        p.WorldDiv_z = divZ;
        p.enableInteractions = enableInteractions ? 1 : 0;
        p.enableColliders = enableColliders ? 1 : 0;
        p.enableForces = enableForces ? 1 : 0;
        p.SolverType = (int)solverType;
        p.LifeTime = particleLifeTime;
        p.Timestep = Time.deltaTime * timeScale;
        p.Decelerate = deceleration;
        p.PressureStiffness = pressureStiffness;
        p.WallStiffness = wallStiffness;
        p.Scaler = coordScale;
        p.ParticleSize = particleSize;
        p.MaxParticles = maxParticleNum;
        MPAPI.mpSetKernelParams(context, ref p);
    }

    void UpdateMPObjects()
    {
        colliders.Clear();
        if (include3DColliders)
        {
            Collider[] colliders3d = Physics.OverlapSphere(transform.position, transform.localScale.magnitude);
            for (int i = 0; i < colliders3d.Length; ++i)
            {
                Collider col = colliders3d[i];
                if (col.isTrigger) { continue; }

                MPColliderProperties cprops;
                bool recv = false;
                var attr = col.gameObject.GetComponent<MPColliderAttribute>();
                if (attr)
                {
                    if (!attr.sendCollision) { continue; }
                    attr.UpdateColliderProperties();
                    recv = attr.receiveCollision;
                    cprops = attr.cprops;
                }
                else
                {
                    cprops = new MPColliderProperties();
                    cprops.SetDefaultValues();
                }
                int id = colliders.Count;
                cprops.owner_id = recv ? id : -1;
                colliders.Add(col.gameObject);

                SphereCollider sphere = col as SphereCollider;
                CapsuleCollider capsule = col as CapsuleCollider;
                BoxCollider box = col as BoxCollider;
                if (sphere)
                {
                    Vector3 pos = sphere.transform.position;
                    MPAPI.mpAddSphereCollider(context, ref cprops, ref pos, sphere.radius * col.gameObject.transform.localScale.magnitude * 0.5f);
                }
                else if (capsule)
                {
                    Vector3 e = Vector3.zero;
                    float h = Mathf.Max(0.0f, capsule.height - capsule.radius * 2.0f);
                    float r = capsule.radius * capsule.transform.localScale.x;
                    switch (capsule.direction)
                    {
                        case 0: e.Set(h * 0.5f, 0.0f, 0.0f); break;
                        case 1: e.Set(0.0f, h * 0.5f, 0.0f); break;
                        case 2: e.Set(0.0f, 0.0f, h * 0.5f); break;
                    }
                    Vector4 pos1 = new Vector4(e.x, e.y, e.z, 1.0f);
                    Vector4 pos2 = new Vector4(-e.x, -e.y, -e.z, 1.0f);
                    pos1 = capsule.transform.localToWorldMatrix * pos1;
                    pos2 = capsule.transform.localToWorldMatrix * pos2;
                    Vector3 pos13 = pos1;
                    Vector3 pos23 = pos2;
                    MPAPI.mpAddCapsuleCollider(context, ref cprops, ref pos13, ref pos23, r);
                }
                else if (box)
                {
                    Matrix4x4 mat = box.transform.localToWorldMatrix;
                    Vector3 size = box.size;
                    MPAPI.mpAddBoxCollider(context, ref cprops, ref mat, ref size);
                }
            }
        }

        if (include2DColliders)
        {
            Vector2 xy = new Vector2(transform.position.x, transform.position.y);
            Collider2D[] colliders2d = Physics2D.OverlapCircleAll(xy, transform.localScale.magnitude);
            for (int i = 0; i < colliders2d.Length; ++i)
            {
                Collider2D col = colliders2d[i];
                if (col.isTrigger) { continue; }

                MPColliderProperties cprops;
                bool recv = false;
                var attr = col.gameObject.GetComponent<MPColliderAttribute>();
                if (attr)
                {
                    if (!attr.sendCollision) { continue; }
                    attr.UpdateColliderProperties();
                    recv = attr.receiveCollision;
                    cprops = attr.cprops;
                }
                else
                {
                    cprops = new MPColliderProperties();
                    cprops.SetDefaultValues();
                }
                int id = colliders.Count;
                cprops.owner_id = recv ? id : -1;
                colliders.Add(col.gameObject);

                CircleCollider2D sphere = col as CircleCollider2D;
                BoxCollider2D box = col as BoxCollider2D;
                if (sphere)
                {
                    Vector3 pos = sphere.transform.position;
                    MPAPI.mpAddSphereCollider(context, ref cprops, ref pos, sphere.radius * col.gameObject.transform.localScale.x);
                }
                else if (box)
                {
                    Matrix4x4 mat = box.transform.localToWorldMatrix;
                    Vector3 size = new Vector3(box.size.x, box.size.y, box.size.x);
                    MPAPI.mpAddBoxCollider(context, ref cprops, ref mat, ref size);
                }
            }
        }

        foreach (MPCollider col in MPCollider.instances)
        {
            if (!col.sendCollision) { continue; }
            col.MPUpdate();
            col.cprops.owner_id = colliders.Count;
            colliders.Add(col.gameObject);
        }
        foreach (MPForce force in MPForce.instances)
        {
            force.MPUpdate();
        }
        foreach (MPEmitter emitter in MPEmitter.instances)
        {
            emitter.MPUpdate();
        }

        if (mprenderer)
        {
            mprenderer.MPUpdate();
        }
    }

    void ExecuteProcessors()
    {
        if (particleProcessor != null)
        {
            particleProcessor(this, MPAPI.mpGetNumParticles(context), MPAPI.mpGetParticles(context));
        }
        if (gatheredHitProcessor != null)
        {
            gatheredHitProcessor(this, MPAPI.mpGetNumHitData(context), MPAPI.mpGetHitData(context));
        }
    }


    public static unsafe void DefaultParticleProcessor(MPWorld world, int numParticles, MPParticle* particles)
    {
        for (int i = 0; i < numParticles; ++i)
        {
            if (particles[i].hit != -1 && particles[i].hit != particles[i].hit_prev)
            {
                GameObject col = world.colliders[particles[i].hit];
                MPColliderAttribute cattr = col.GetComponent<MPColliderAttribute>();
                if (cattr)
                {
                    cattr.particleHitHandler(world, col, ref particles[i]);
                }
            }
        }
    }

    public static unsafe void DefaultGatheredHitProcessor(MPWorld world, int numColliders, MPHitData* hits)
    {
        for (int i = 0; i < numColliders; ++i)
        {
            if (hits[i].num_hits>0)
            {
                GameObject col = world.colliders[i];
                MPColliderAttribute cattr = col.GetComponent<MPColliderAttribute>();
                if (cattr)
                {
                    cattr.gatheredHitHandler(world, col, ref hits[i]);
                }
            }
        }
    }
}
