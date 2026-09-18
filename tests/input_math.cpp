#include "controller_input.hpp"
#include "puzzle_aim.hpp"
#include "snap_turn.hpp"
#include "stick_cursor.hpp"
#include "input_settings.hpp"
#include "puzzle_back.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace witness::input;
void check_impl(bool v,int line){if(!v)throw std::runtime_error("Input invariant failed at line " + std::to_string(line));}
#define check(...) check_impl((__VA_ARGS__),__LINE__)
int main() {
    try {
        Movement m; Hand h;h.device=2;h.valid=true;h.stick=2;h.types[2]=2;h.state.axes[2]={0,1};
        check(m.update(h,true).y==0);h.state.axes[2]={};check(m.update(h,true).y==0);
        h.state.axes[2]={0,.6f};check(std::abs(m.update(h,true).y-.5f)<.0001f);
        check(m.update(h,false).y==0);check(m.update(h,true).y==0);
        h.state.axes[2]={};m.update(h,true);h.state.axes[2]={0,1};check(m.update(h,true).y==1);
        h.device=5;check(m.update(h,true).y==0);h.state.axes[2]={};m.update(h,true);
        h.valid=false;check(m.update(h,true).y==0);h.valid=true;h.state.axes[2]={0,1};check(m.update(h,true).y==0);
        h.state.axes[2]={};m.update(h,true);h.state.axes[2]={std::numeric_limits<float>::quiet_NaN(),1};check(m.update(h,true).y==0);
        h.state.axes[2]={0,1};check(m.update(h,true).y==0);
        check(deadzone({.1f,.1f}).x==0);const auto a=deadzone({1,1});check(std::abs(std::hypot(a.x,a.y)-1)<.0001f);
        Hand legacy;legacy.types[0]=1;legacy.types[1]=3;legacy.types[2]=3;legacy.types[3]=-1;legacy.types[4]=-1;
        select_axis(legacy,false);check(legacy.stick==-1);select_axis(legacy,true);check(legacy.stick==0 && legacy.legacy_axis);
        legacy.types[2]=2;select_axis(legacy,true);check(legacy.stick==2 && !legacy.legacy_axis);
        legacy.types[3]=2;select_axis(legacy,true);check(legacy.stick==-1);
        legacy.types[2]=0;legacy.types[3]=0;select_axis(legacy,true);check(legacy.stick==-1);
        Hand buttons;buttons.valid=true;buttons.types[0]=1;buttons.types[1]=3;buttons.types[2]=3;buttons.types[3]=-1;buttons.types[4]=-1;
        select_axis(buttons,true);buttons.state.pressed=1ull<<2;check(recenter_pressed(buttons));
        buttons.state.pressed=1ull<<7;check(!recenter_pressed(buttons));
        select_axis(buttons,false);check(recenter_pressed(buttons));
        buttons.state.pressed=1ull<<2;check(!recenter_pressed(buttons));
        buttons.types[2]=2;select_axis(buttons,true);check(!recenter_pressed(buttons));
        buttons.state.pressed=1ull<<7;check(recenter_pressed(buttons));
        buttons.valid=false;check(!recenter_pressed(buttons));
        SnapTurn snap; Hand right_hand;right_hand.valid=true;right_hand.device=1;right_hand.stick=2;right_hand.types[2]=2;
        std::uint64_t snap_tick=1000;
        const auto turn=[&](Axis axis,bool allowed=true) {right_hand.state.axes[2]=axis;snap_tick+=10;return snap.update(right_hand,allowed,snap_tick);};
        check(turn({1,0})==0);check(turn({})==0);check(turn({.69f,0})==0);check(turn({.8f,0})==1);
        for(int i=0;i<30;++i)check(turn({1,0})==0); // Holding never repeats, including past cooldown.
        check(turn({})==0);check(turn({-1,0})==-1);
        check(turn({})==0);check(turn({1,0})==0); // A too-fast second flick is consumed, not delayed.
        for(int i=0;i<30;++i)check(turn({1,0})==0);
        check(turn({})==0);check(turn({0,1})==0);check(turn({1,0})==0); // Sliding from vertical is not a new edge.
        check(turn({})==0);check(turn({1,0})==1);
        check(turn({},false)==0);check(turn({1,0})==0); // Cannot arm in a puzzle/menu.
        check(turn({})==0);check(turn({1,0})==1);
        right_hand.device=3;check(turn({-1,0})==0);check(turn({})==0);check(turn({-1,0})==-1);
        check(turn({})==0);right_hand.valid=false;check(turn({})==0);right_hand.valid=true;check(turn({1,0})==0);
        check(turn({})==0);snap_tick+=101;check(turn({1,0})==0); // Stale input must recenter.
        check(turn({})==0);check(turn({std::numeric_limits<float>::quiet_NaN(),0})==0);check(turn({1,0})==0);
        check(turn({})==0);right_hand.types[2]=1;check(turn({1,0})==0);right_hand.types[2]=2;check(turn({1,0})==0);
        right_hand.stick=0;right_hand.types[0]=1;right_hand.legacy_axis=true;right_hand.state.axes[0]={};
        check(snap.update(right_hand,true,snap_tick+=10)==0);right_hand.state.axes[0]={-1,0};check(snap.update(right_hand,true,snap_tick+=10)==-1);
        PuzzleBack back;Hand left;left.valid=true;left.device=4;left.state.pressed=2;
        check(!back.update(left,true));left.state.pressed=0;check(!back.update(left,true));
        left.state.pressed=2;check(back.update(left,true));check(!back.update(left,true));
        check(!back.update(left,false));check(!back.update(left,true));
        left.state.pressed=0;check(!back.update(left,true));left.state.pressed=2;check(back.update(left,true));
        left.device=5;check(!back.update(left,true));left.state.pressed=0;back.update(left,true);
        left.valid=false;check(!back.update(left,true));left.valid=true;left.state.pressed=2;check(!back.update(left,true));
        std::uint32_t configured_speed=40;
        check(parse_cursor_speed(L"20",configured_speed)&&configured_speed==20);
        check(parse_cursor_speed(L" 300 ",configured_speed)&&configured_speed==300);
        check(parse_cursor_speed(L"10",configured_speed)&&configured_speed==10);
        for(auto bad:{L"",L"0",L"9",L"301",L"-40",L"20.5",L"40junk",L"999999999999999999"}) {
            check(!parse_cursor_speed(bad,configured_speed)&&configured_speed==10);
        }
        StickCursor manual;Hand manual_hand;manual_hand.device=1;manual_hand.valid=true;manual_hand.stick=2;manual_hand.types[2]=2;
        Axis manual_delta;std::uint64_t manual_tick=1000;
        const auto nudge=[&](Axis axis,bool allowed=true,bool recenter=false) {
            manual_hand.state.axes[2]=axis;manual_tick+=10;
            return manual.update(manual_hand,allowed,recenter,manual_tick,1.f,.8f,manual_delta);
        };
        check(!nudge({1,0}));check(!nudge({}));check(nudge({1,0})&&std::abs(manual_delta.x-.008f)<.000001f);
        check(nudge({})&&manual_delta.x==0&&manual_delta.y==0); // Rest does not return to pointing.
        check(nudge({-1,0})&&manual_delta.x<0);check(nudge({0,1})&&manual_delta.y>0);
        check(!nudge({},true,true));check(!nudge({})); // A releases ownership.
        check(nudge({1,0}));check(!nudge({1,0},false));check(!nudge({1,0}));check(!nudge({}));
        check(nudge({1,0}));manual_hand.device=3;check(!nudge({1,0}));check(!nudge({}));check(nudge({1,0}));
        manual_tick+=101;check(!nudge({1,0}));check(!nudge({}));check(nudge({1,0}));
        check(!nudge({std::numeric_limits<float>::infinity(),0}));
        const auto stick_distance=[](unsigned dt,float speed=.8f) {
            StickCursor sc;Hand h;h.device=1;h.valid=true;h.stick=2;h.types[2]=2;Axis d;float sum=0;
            sc.update(h,true,false,1000,1,speed,d);h.state.axes[2]={1,0};
            for(unsigned t=dt;t<=100;t+=dt){check(sc.update(h,true,false,1000+t,1,speed,d));sum+=d.x;}
            return sum;
        };
        check(std::abs(stick_distance(10)-.08f)<.000001f&&std::abs(stick_distance(20)-.08f)<.000001f);
        check(std::abs(stick_distance(10,.4f)-stick_distance(10,.8f)/2.f)<.000001f);
        float next_yaw{},next_vr{};
        for(unsigned steps:{1u,2u,4u})for(int direction:{-1,1}) {
            check(snap_headings(1.25f,-.5f,direction,steps,next_yaw,next_vr));
            const float expected=-direction*static_cast<float>(steps)*3.14159265358979323846f/8.f;
            check(std::abs(next_yaw-1.25f-expected)<.000001f&&std::abs(next_vr+.5f-expected)<.000001f);
        }
        check(!snap_headings(0,0,0,2,next_yaw,next_vr));check(!snap_headings(0,0,1,3,next_yaw,next_vr));
        check(!snap_headings(std::numeric_limits<float>::infinity(),0,1,2,next_yaw,next_vr));
        check(!snap_headings(0,10001,1,2,next_yaw,next_vr));
        Context c{2,true,true,false,0,{1,0,0},{0,1,0}};
        Vec3 v{};check(blend(v,{1,0},c)&&v.y==-1&&v.x==0);
        v={1,0,0};check(blend(v,{1,0},c)&&length(v)<=1.00001f);
        v={.1f,0,0};check(!blend(v,{},c)&&v.x==.1f);
        check(walking(c));c.mode=1;check(!walking(c));c.mode=3;check(!walking(c));c.mode=2;c.fade=.01f;check(!walking(c));
        TrackedPose head;head.valid=true;head.connected=true;head.tracking_result=200;
        for(int i=0;i<3;++i)head.matrix[i][i]=1;
        auto hand=head;
        AimFrame frame{{.5f,.5f},{1,1},1,{0,0,0},{-1,-1,-2},{1,0,0},{0,1,0},true};
        Axis target;
        check(aim_target(head,hand,frame,target)&&std::abs(target.x-.5f)<.0001f&&std::abs(target.y-.5f)<.0001f);
        hand.matrix[0][0]=std::cos(.2f);hand.matrix[0][2]=-std::sin(.2f);
        hand.matrix[2][0]=std::sin(.2f);hand.matrix[2][2]=std::cos(.2f);
        check(aim_target(head,hand,frame,target)&&std::abs(target.x-(.5f+std::tan(.2f)))<.0001f);
        check(aim_target(hand,hand,frame,target)&&std::abs(target.x-.5f)<.0001f);
        auto bad=hand;bad.valid=false;check(!aim_target(head,bad,frame,target));
        bad=hand;bad.matrix[0][0]=std::numeric_limits<float>::quiet_NaN();check(!aim_target(head,bad,frame,target));
        bad=head;bad.matrix[0][0]=-1;check(!valid_pose(bad));
        auto bad_frame=frame;bad_frame.u=bad_frame.v;check(!aim_target(head,hand,bad_frame,target));
        bad_frame=frame;bad_frame.scale=0;check(!aim_target(head,hand,bad_frame,target));
        bad=head;bad.matrix[0][0]=-1;bad.matrix[2][2]=-1;check(!aim_target(head,bad,frame,target));
        Pointing pointer;Axis delta;std::uint64_t tick=1000;
        const auto update=[&](std::uint32_t device,bool allowed,bool trigger,Axis target) {
            tick+=10;return pointer.update(device,allowed,trigger,target,frame.cursor,delta,tick,1,AimSettings{});
        };
        check(!update(1,true,true,{.7f,.5f}));
        check(!update(1,true,false,{.7f,.5f}));
        check(update(1,true,true,{.7f,.5f})&&std::abs(delta.x-.008f)<.0001f);
        check(!update(1,false,true,{.7f,.5f}));
        check(!update(1,true,true,{.7f,.5f}));
        check(!update(1,true,false,{.7f,.5f}));
        check(update(1,true,false,{.5f,.5f})&&delta.x==0&&delta.y==0);
        check(!update(3,true,true,{.7f,.5f}));
        check(!update(3,true,false,{.7f,.5f}));tick+=150;
        check(!update(3,true,true,{.7f,.5f}));
        // The exponential response has the same elapsed-time behavior at 50/100 Hz.
        const auto simulate=[](unsigned dt,AimSettings settings,Axis target,unsigned duration) {
            Pointing p;Axis current{.5f,.5f},d{};
            p.update(1,true,false,target,current,d,1000,1,settings);
            for(unsigned t=dt;t<=duration;t+=dt) {
                check(p.update(1,true,false,target,current,d,1000+t,1,settings));
                check(std::hypot(d.x,d.y)<=settings.speed*dt/1000.f+.00001f);
                current.x+=d.x;current.y+=d.y;
            }
            return current;
        };
        const auto fast=simulate(10,{.8f,80},{.55f,.5f},200), slow=simulate(20,{.8f,80},{.55f,.5f},200);
        check(std::abs(fast.x-slow.x)<.00001f);
        check(std::abs(fast.x-(.55f-.05f*std::exp(-2.5f)))<.00001f);
        check(std::abs(simulate(10,{.4f,0},{.9f,.5f},100).x-.54f)<.00001f);
        check(std::abs(simulate(20,{.4f,0},{.9f,.5f},100).x-.54f)<.00001f);
        check(!valid_settings({0,80})&&!valid_settings({.8f,251})&&!valid_settings({std::numeric_limits<float>::infinity(),0}));
        AimCalibration calibration;
        check(!calibration.update(1,true,true,false,head,hand)); // Held on entry cannot recenter.
        check(!calibration.update(1,true,false,false,head,hand));
        check(calibration.update(1,true,true,false,head,hand));
        check(aim_target(head,hand,frame,target,calibration)&&std::abs(target.x-.5f)<.0001f);
        check(!calibration.update(1,true,true,false,head,head)); // Held A must not continually calibrate.
        check(aim_target(head,hand,frame,target,calibration)&&std::abs(target.x-.5f)<.0001f);
        calibration.disarm();
        check(!calibration.update(1,true,true,false,head,hand));
        calibration.update(1,true,false,false,head,hand);
        check(!calibration.update(1,false,true,false,head,hand));
        check(!calibration.update(1,true,true,false,head,hand));
        calibration.update(1,true,false,false,head,hand);
        check(!calibration.update(1,true,true,true,head,head)); // Drawing cannot recenter.
        check(aim_target(head,hand,frame,target,calibration)&&std::abs(target.x-.5f)<.0001f);
        calibration.update(3,true,true,false,head,hand); // Device replacement clears the old reference.
        check(aim_target(head,hand,frame,target,calibration)&&target.x>.6f);
        calibration.update(3,true,false,false,head,hand);
        check(calibration.update(3,true,true,false,head,hand));
        // Turning the head must not alter the calibrated tracking-space hand ray.
        const auto corrected=calibration.direction(hand);
        check(std::abs(corrected.x)<.0001f&&std::abs(corrected.z+1.f)<.0001f);
        check(aim_target(hand,hand,frame,target,calibration)&&target.x<.4f);
        Context aim_context{0,true,true,false,0,{},{}};check(puzzle(aim_context));
        aim_context.mode=1;check(puzzle(aim_context));aim_context.mode=2;check(!puzzle(aim_context));
        std::cout<<"Input neutral rearm, invalid state, role changes, axis selection, deadzone, blend and gates passed.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
