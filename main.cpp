/* Copyright (c) 2010 Mosalam Ebrahimi <m.ebrahimi@ieee.org>
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in
*  all copies or substantial portions of the Software.
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
*  THE SOFTWARE.
*/

#include <osg/GL>
#include <osgViewer/Viewer>
#include <osg/MatrixTransform>
#include <osg/Geode>
#include <osg/Group>
#include <osgGA/TrackballManipulator>
#include <osgSim/LightPointNode>
#include <pthread.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>

#include <libfreenect/libfreenect.h>

#include "tweakbargui.h"

volatile int die = 0;
pthread_t freenect_thread;

freenect_context *f_ctx;
freenect_device *f_dev;

double depth_data[240][320];

pthread_mutex_t backbuf_mutex = PTHREAD_MUTEX_INITIALIZER;

int frame = 0;

using namespace std;
using namespace osgSim;

void addToLightPointNode(LightPointNode& lpn, unsigned int noSteps,
						 unsigned int j, bool bexport, ostringstream& sstream)
{
        lpn.getLightPointList().reserve(noSteps);

        for ( unsigned int i=0;i<noSteps;++i) {
                LightPoint lp;
				lp._color.set(1.0f, 1.0f, 1.0f, 1.0f);
                lp._position.z() = depth_data[j][i] * 14.0;
                lp._position.y() = (j-120.0) * (lp._position.z() + -10.0)
										* 0.005;
                lp._position.x() = (i-160.0) * (lp._position.z() + -10.0)
										* 0.005;

                if (depth_data[j][i] <= ( 100.0/3.33 ))
						continue;
				
				lpn.addLightPoint(lp);
				
				if (bexport) {
						sstream<<(int)lp._position.x()<<" "
							<<(int)lp._position.y()<<" "
							<<(int)lp._position.z()<<endl;
				}
        }
}

osg::Node* createLightPointsDatabase(ExportState* export_state)
{
        osg::MatrixTransform* transform = new osg::MatrixTransform;

        transform->setDataVariance(osg::Object::STATIC);
        transform->setMatrix(osg::Matrix::scale(0.0002, 0.0002, 0.0002));

        int noStepsX = 320;
        int noStepsY = 240;

		ofstream file;
		if (export_state->state) {
				file.open("kinect.ply");
				file<<"ply"<<endl;
				file<<"format ascii 1.0"<<endl;
				file<<"comment created by Angkor"<<endl;
				file<<"comment (yet another Kinect point cloud viewer)"<<endl;
				file<<"element vertex ";
		}
		
        pthread_mutex_lock(&backbuf_mutex);

		ostringstream ss;
		size_t totall_points = 0;
        for (int i=0;i<noStepsY;++i) {

                LightPointNode* lpn = new LightPointNode;

                addToLightPointNode(*lpn, noStepsX, i, export_state->state, ss);
				
				totall_points += lpn->getNumLightPoints();

                transform->addChild(lpn);
        }
        pthread_mutex_unlock(&backbuf_mutex);

        osg::Group* group = new osg::Group;
        group->addChild(transform);
		
		if (export_state->state) {
				file<<totall_points<<endl;
				file<<"property float x"<<endl;
				file<<"property float y"<<endl;
				file<<"property float z"<<endl;
				file<<"end_header"<<endl;
				file<<ss.str();
				export_state->state = false;
				file.close();
		}


        return group;
}

void depth_cb(freenect_device *dev, void *v_depth, uint32_t timestamp)
{
        uint16_t *depth = ( uint16_t * ) v_depth;
        pthread_mutex_lock(&backbuf_mutex);
        for ( int i=0; i<240; i++ ) {
                for ( int j=0; j<320; j++ ) {
                        double metric = 100.0 / ( -0.00307 * 
								depth[ ( ( i*2 ) * 640)	+ ( j*2 ) ] + 3.33 );
                        depth_data[i][j] = metric;
                }
        }
        pthread_mutex_unlock(&backbuf_mutex);
        frame++;
}

void *freenect_threadfunc(void *arg)
{
        freenect_set_depth_callback(f_dev, depth_cb);
		freenect_set_depth_mode(f_dev, freenect_find_depth_mode(
			                             FREENECT_RESOLUTION_MEDIUM, 
										 FREENECT_DEPTH_11BIT));

        int res = freenect_start_depth(f_dev);
		if (res !=0) {
			cout<<"freenect_start_depth failed!"<<endl;
		}

        while ( !die && freenect_process_events(f_ctx) >= 0 ) {
                freenect_raw_tilt_state* state;
				state = freenect_get_tilt_state(f_dev);
                double dx,dy,dz;
                freenect_get_mks_accel(state, &dx, &dy, &dz);
                cout<<"\r frame: "<<frame<<
						" - mks acc: "<<dx<<" "<<dy<<" "<<dz<<"\r";
        }

        cout<<"\nshutting down streams...\n";

        freenect_stop_depth(f_dev);

        freenect_close_device(f_dev);
        freenect_shutdown(f_ctx);

        cout<<"-- done!\n";
        return NULL;
}


int main(int argc, char **argv)
{
        for (int i=0; i<240; i++)
                for (int j=0; j<320; j++)
                        depth_data[i][j] = 0;

        if (freenect_init(&f_ctx, NULL) < 0) {
                cout<<"freenect_init() failed\n";
                return 1;
        }

        freenect_set_log_level(f_ctx, FREENECT_LOG_DEBUG);
        if (freenect_open_device(f_ctx, &f_dev, 0) < 0) {
                cout<<"Could not open device\n";
                return 1;
        }
        
		int res = pthread_create(&freenect_thread, NULL, freenect_threadfunc,
								 NULL);
        if (res) {
                printf("pthread_create failed\n");
                return 1;
        }
        
        osgViewer::Viewer viewer;
        viewer.setCameraManipulator (new osgGA::TrackballManipulator());
		osg::Vec3d eye(0.0,0.0,-1.0);
		osg::Vec3d center(0.0,0.0,0.0);
		osg::Vec3d up(0.0,-1.0,0.0);
		viewer.getCameraManipulator()->setHomePosition(eye, center, up);
        viewer.setUpViewInWindow(640, 0, 640, 480);
		ExportState export_state;
		TweakBarEventCallback* e_handler;
		e_handler = new TweakBarEventCallback(&export_state);
		viewer.addEventHandler(e_handler);
		osgViewer::ViewerBase::Windows wins;
		viewer.getWindows(wins);
		wins[0]->setWindowName("Angkor");
		viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
        viewer.realize();

		osg::ref_ptr<osg::Geode> geode = new osg::Geode;
		osg::ref_ptr<TweakBarDrawable> cd = new TweakBarDrawable();
		geode->addDrawable(cd.get());
		osg::StateSet* ss = geode->getOrCreateStateSet();
		ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
		
		osg::Camera* hudCam = cd->createHUD(wins[0]->getTraits()->width,
											wins[0]->getTraits()->height);
		hudCam->addChild(geode);
	
        osg::Group* rootnode = new osg::Group;
		rootnode->addChild(hudCam);

        osg::Node* lps;
        lps = createLightPointsDatabase(&export_state);
        rootnode->addChild(lps);
        viewer.setSceneData(rootnode);

        while ( !viewer.done() ) {
                rootnode->removeChild(lps);
                lps = createLightPointsDatabase(&export_state);
                rootnode->addChild(lps);
                viewer.setSceneData(rootnode);
                viewer.frame();
        }
        die = 1;
        sleep(1);

        return 0;
}
