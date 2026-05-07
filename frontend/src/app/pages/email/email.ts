import { Component, inject, signal, OnInit } from '@angular/core';
import { ActivatedRoute, Router } from '@angular/router';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { MatTabsModule } from '@angular/material/tabs';
import { DomSanitizer, SafeHtml } from '@angular/platform-browser';
import { GmailService } from '../../services/gmail.service';
import { EmailDetail } from '../../models/email.model';

@Component({
  selector: 'app-email',
  standalone: true,
  imports: [
    CommonModule, MatButtonModule, MatIconModule,
    MatProgressSpinnerModule, MatTabsModule
  ],
  template: `
    <div class="page-container">
      <div class="back-row">
        <button mat-button (click)="back()">
          <mat-icon>arrow_back</mat-icon> Back
        </button>
      </div>

      <div *ngIf="loading()" class="loading-center">
        <mat-spinner diameter="40"></mat-spinner>
      </div>

      <div *ngIf="error()" class="error-msg">{{ error() }}</div>

      <ng-container *ngIf="email() as e">
        <div class="card email-header">
          <div class="email-subject">{{ e.subject || '(no subject)' }}</div>
          <div class="email-meta">
            <span class="label">From:</span> {{ e.from }}
          </div>
          <div class="email-meta">
            <span class="label">Date:</span> {{ e.date | date:'full' }}
          </div>
          <div class="email-meta" *ngIf="e.thread_id">
            <span class="label">Thread:</span>
            <a (click)="openThread(e.thread_id)" style="cursor:pointer">View thread</a>
          </div>
        </div>

        <div class="card">
          <mat-tab-group *ngIf="e.body_html; else textOnly">
            <mat-tab label="HTML">
              <div class="body-html" [innerHTML]="safeHtml()"></div>
            </mat-tab>
            <mat-tab label="Plain text">
              <pre class="body-text">{{ e.body_text }}</pre>
            </mat-tab>
          </mat-tab-group>
          <ng-template #textOnly>
            <pre class="body-text">{{ e.body_text || '(no body)' }}</pre>
          </ng-template>
        </div>
      </ng-container>
    </div>
  `,
  styles: [`
    .back-row { margin-bottom: 12px; }
    .loading-center { display: flex; justify-content: center; padding: 40px; }
    .error-msg { color: #f44336; padding: 16px; }
    .email-header { }
    .email-subject { font-size: 20px; font-weight: 500; margin-bottom: 12px; }
    .email-meta { font-size: 13px; color: #aaa; margin-bottom: 4px; }
    .label { color: #aaa; margin-right: 4px; }
    .body-text { white-space: pre-wrap; word-break: break-word; font-size: 13px; color: #ddd; margin: 0; font-family: monospace; }
    .body-html { padding: 8px; color: #ddd; font-size: 14px; overflow: auto; }
  `]
})
export class EmailPage implements OnInit {
  private route = inject(ActivatedRoute);
  private router = inject(Router);
  private gmail = inject(GmailService);
  private sanitizer = inject(DomSanitizer);

  email = signal<EmailDetail | null>(null);
  loading = signal(true);
  error = signal('');
  safeHtml = signal<SafeHtml>('');

  ngOnInit() {
    const id = Number(this.route.snapshot.paramMap.get('id'));
    this.gmail.getEmail(id).subscribe({
      next: (e) => {
        this.email.set(e);
        if (e.body_html) {
          this.safeHtml.set(this.sanitizer.bypassSecurityTrustHtml(e.body_html));
        }
        this.loading.set(false);
      },
      error: (err) => {
        this.error.set(err.error?.error || 'Failed to load email');
        this.loading.set(false);
      }
    });
  }

  back() { this.router.navigate(['/search']); }
  openThread(tid: string) { this.router.navigate(['/threads', tid]); }
}
