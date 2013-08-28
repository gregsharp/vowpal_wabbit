using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;

using Microsoft.Research.MachineLearning;
using System.Runtime.InteropServices;

namespace cs_test
{
    class Program
    {
        static void Main(string[] args)
        {
            //RunFeaturesTest();
            //RunParserTest();
            RunParserTest2();
        }

        private static void RunFeaturesTest()
        {
            // this usually requires that the library script to update train.w or its moral equivalent needs to have been run 
            IntPtr vw = VowpalWabbitInterface.Initialize("-q st --noconstant --quiet");
            IntPtr example = VowpalWabbitInterface.ReadExample(vw, "1 |s p^the_man w^the w^man |t p^un_homme w^un w^homme");
            float score = VowpalWabbitInterface.Learn(vw, example);
            VowpalWabbitInterface.FinishExample(vw, example);

            VowpalWabbitInterface.FEATURE_SPACE[] featureSpace = new VowpalWabbitInterface.FEATURE_SPACE[2];//maximum number of index spaces

            VowpalWabbitInterface.FEATURE[] sfeatures = new VowpalWabbitInterface.FEATURE[3];// the maximum number of features
            VowpalWabbitInterface.FEATURE[] tfeatures = new VowpalWabbitInterface.FEATURE[3];// the maximum number of features

            GCHandle pinnedsFeatures = GCHandle.Alloc(sfeatures, GCHandleType.Pinned);
            GCHandle pinnedtFeatures = GCHandle.Alloc(tfeatures, GCHandleType.Pinned);

            featureSpace[0].features = pinnedsFeatures.AddrOfPinnedObject();
            featureSpace[1].features = pinnedtFeatures.AddrOfPinnedObject();

            GCHandle pinnedFeatureSpace = GCHandle.Alloc(featureSpace, GCHandleType.Pinned);

            IntPtr featureSpacePtr = pinnedFeatureSpace.AddrOfPinnedObject();

            uint snum = VowpalWabbitInterface.HashSpace(vw, "s");
            featureSpace[0].name = (byte)'s';
            sfeatures[0].weight_index = VowpalWabbitInterface.HashFeature(vw, "p^the_man", snum);
            sfeatures[0].x = 1;
            // add the character "delta" to test unicode
            // do it as a string to test the marshaling is doing pinning correctly.
            const string s = "w^thew^man\u0394";
            sfeatures[1].weight_index = VowpalWabbitInterface.HashFeature(vw, s, snum);
            sfeatures[1].x = 1;
            sfeatures[2].weight_index = VowpalWabbitInterface.HashFeature(vw, "w^man", snum);
            sfeatures[2].x = 1;
            featureSpace[0].len = 3;

            uint tnum = VowpalWabbitInterface.HashSpace(vw, "t");
            featureSpace[1].name = (byte)'t';
            tfeatures[0].weight_index = VowpalWabbitInterface.HashFeature(vw, "p^un_homme", tnum);
            tfeatures[0].x = 1;
            tfeatures[1].weight_index = VowpalWabbitInterface.HashFeature(vw, "w^un", tnum);
            tfeatures[1].x = 1;
            tfeatures[2].weight_index = VowpalWabbitInterface.HashFeature(vw, "w^homme", tnum);
            tfeatures[2].x = 1;
            featureSpace[1].len = 3;

            IntPtr importedExample = VowpalWabbitInterface.ImportExample(vw, featureSpacePtr, featureSpace.Length);

            VowpalWabbitInterface.AddLabel(importedExample, 1);

            score = VowpalWabbitInterface.Learn(vw, importedExample);

            Console.Error.WriteLine("p2 = {0}", score);

            VowpalWabbitInterface.Finish(vw);

            // clean up the memory we allocated
            pinnedsFeatures.Free();
            pinnedtFeatures.Free();
            pinnedFeatureSpace.Free();
        }

        private static void RunParserTest()
        {
            IntPtr vw = VowpalWabbitInterface.Initialize("-q st -d 0002.dat -f out");

            VowpalWabbitInterface.StartParser(vw, false);

            int count = 0;
            IntPtr example = IntPtr.Zero;
            while (IntPtr.Zero != (example = VowpalWabbitInterface.GetExample(vw)))
            {
                example = VowpalWabbitInterface.GetExample(vw);
                count++;
                int featureSpaceLen = 0;
                IntPtr featureSpacePtr = VowpalWabbitInterface.ExportExample(vw, example, ref featureSpaceLen);

                VowpalWabbitInterface.FEATURE_SPACE[] featureSpace = new VowpalWabbitInterface.FEATURE_SPACE[featureSpaceLen];
                int featureSpace_size = Marshal.SizeOf(typeof(VowpalWabbitInterface.FEATURE_SPACE));

                for (int i = 0; i < featureSpaceLen; i++)
                {
                    IntPtr curfeatureSpacePos = new IntPtr(featureSpacePtr.ToInt32() + i * featureSpace_size);
                    featureSpace[i] = (VowpalWabbitInterface.FEATURE_SPACE)Marshal.PtrToStructure(curfeatureSpacePos, typeof(VowpalWabbitInterface.FEATURE_SPACE));

                    VowpalWabbitInterface.FEATURE[] feature = new VowpalWabbitInterface.FEATURE[featureSpace[i].len];
                    int feature_size = Marshal.SizeOf(typeof(VowpalWabbitInterface.FEATURE));
                    for (int j = 0; j < featureSpace[i].len; j++)
                    {
                        IntPtr curfeaturePos = new IntPtr((featureSpace[i].features.ToInt32() + j * feature_size));
                        feature[j] = (VowpalWabbitInterface.FEATURE)Marshal.PtrToStructure(curfeaturePos, typeof(VowpalWabbitInterface.FEATURE));
                    }
                }
                VowpalWabbitInterface.ReleaseFeatureSpace(featureSpacePtr, featureSpaceLen);


                float score = VowpalWabbitInterface.Learn(vw, example);
                VowpalWabbitInterface.FinishExample(vw, example);
            }

            VowpalWabbitInterface.EndParser(vw);

            VowpalWabbitInterface.Finish(vw);
        }


        private static void RunParserTest2()
        {
            long maxEx = 10; //long.MaxValue

            //            IntPtr vw = VowpalWabbitInterface.Initialize("-q st -d 0002.dat -f out");
            IntPtr vw = VowpalWabbitInterface.Initialize("-q st -d 0002.dat -b 18 --hash strings -f out --ring_size 74748 --examples 10");

            VowpalWabbitInterface.StartParser(vw, false);

            int count = 0;
            IntPtr example = IntPtr.Zero;
            while (count < maxEx)
            {
                example = VowpalWabbitInterface.GetExample(vw);
                if (IntPtr.Zero == example)
                    break;

                count++;


                //int featureSpaceLen = 0;
                //IntPtr featureSpacePtr = VowpalWabbitInterface.ExportExample(vw, example, ref featureSpaceLen);

                //VowpalWabbitInterface.FEATURE_SPACE[] featureSpace = new VowpalWabbitInterface.FEATURE_SPACE[featureSpaceLen];
                //int featureSpace_size = Marshal.SizeOf(typeof(VowpalWabbitInterface.FEATURE_SPACE));

                //for (int i = 0; i < featureSpaceLen; i++)
                //{
                //    IntPtr curfeatureSpacePos = new IntPtr(featureSpacePtr.ToInt32() + i * featureSpace_size);
                //    featureSpace[i] = (VowpalWabbitInterface.FEATURE_SPACE)Marshal.PtrToStructure(curfeatureSpacePos, typeof(VowpalWabbitInterface.FEATURE_SPACE));

                //    VowpalWabbitInterface.FEATURE[] feature = new VowpalWabbitInterface.FEATURE[featureSpace[i].len];
                //    int feature_size = Marshal.SizeOf(typeof(VowpalWabbitInterface.FEATURE));
                //    for (int j = 0; j < featureSpace[i].len; j++)
                //    {
                //        IntPtr curfeaturePos = new IntPtr((featureSpace[i].features.ToInt32() + j * feature_size));
                //        feature[j] = (VowpalWabbitInterface.FEATURE)Marshal.PtrToStructure(curfeaturePos, typeof(VowpalWabbitInterface.FEATURE));
                //    }
                //}
                //VowpalWabbitInterface.ReleaseFeatureSpace(featureSpacePtr, featureSpaceLen);


                //float score = VowpalWabbitInterface.Learn(vw, example);
                //VowpalWabbitInterface.FinishExample(vw, example);
            }

            VowpalWabbitInterface.EndParser(vw);

            VowpalWabbitInterface.Finish(vw);
        }

    }
}
